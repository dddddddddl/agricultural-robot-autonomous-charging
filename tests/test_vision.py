"""Exercise supplied vision math without a ROS installation or live camera.

The source AST is loaded with message/base-class names stubbed. No node is
constructed, ROS callbacks are not run, and no motion commands are transmitted.
"""
import ast
import math
import time
import unittest
from pathlib import Path

import cv2
import numpy as np

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'robot' / 'vision' / 'aruco_docking.py'
module = ast.parse(SOURCE.read_text())
module.body = [node for node in module.body if isinstance(
    node, (ast.Assign, ast.AnnAssign, ast.FunctionDef, ast.ClassDef)
)]
scope = {'Node': object, 'Twist': object, 'Bool': object, 'Int8': object,
         'math': math, 'time': time, 'np': np, 'cv2': cv2}
exec(compile(module, str(SOURCE), 'exec'), scope)
Controller = scope['ArucoDockingWithView']


class VisionTests(unittest.TestCase):
    def controller(self):
        controller = Controller.__new__(Controller)
        controller.is_charging = False
        controller.reset_control_filters()
        return controller

    def test_stop_conditions_and_reverse_alignment(self):
        controller = self.controller()
        self.assertEqual(controller.decide_cmd(False, None, None, None), ('NO_MARKER', 0.0, 0.0))
        self.assertEqual(controller.decide_cmd(True, 0, -1, 0), ('INVALID_Z', 0.0, 0.0))
        self.assertEqual(controller.decide_cmd(True, 0, 4, 0), ('TOO_FAR_WAIT_NAV2', 0.0, 0.0))
        self.assertEqual(controller.decide_cmd(True, 0, 0.16, 0), ('STOP_DISTANCE_REACHED', 0.0, 0.0))
        controller.is_charging = True
        self.assertEqual(controller.decide_cmd(False, None, None, None), ('CHARGING_DETECTED', 0.0, 0.0))
        controller.is_charging = False
        state, linear, angular = controller.decide_cmd(True, 0.06, 0.5, 8)
        self.assertEqual(state, 'ALIGNING')
        self.assertAlmostEqual(linear, -0.018)
        self.assertLessEqual(abs(angular), scope['MAX_TURN_SPEED'])
        controller.reset_control_filters()
        self.assertEqual(controller.decide_cmd(True, 0, 0.5, 0), ('REVERSING', -0.03, 0.0))

    def test_pose_solution_from_projected_marker(self):
        controller = self.controller()
        controller.object_points = scope['create_marker_object_points'](scope['MARKER_LENGTH'])
        # The front-facing marker has its local Y axis pointing upward.
        rvec = np.array([[math.pi], [0.0], [0.0]])
        tvec = np.array([[0.02], [0.01], [0.6]])
        points, _ = cv2.projectPoints(controller.object_points, rvec, tvec,
                                     scope['camera_matrix'], scope['dist_coeffs'])
        result = controller.compute_pose([points.reshape(1, 4, 2)], np.array([[0]]))
        self.assertTrue(result[0])
        np.testing.assert_allclose(result[1:4], tvec.ravel(), atol=1e-5)

    def test_calibration_artifacts_and_marker_are_consistent(self):
        folder = ROOT / 'robot' / 'calibration'
        storage = cv2.FileStorage(str(folder / 'ov9281_calibration.yaml'), cv2.FILE_STORAGE_READ)
        np.testing.assert_allclose(storage.getNode('camera_matrix').mat(), scope['camera_matrix'], rtol=1e-6)
        self.assertEqual(int(storage.getNode('good_image_count').real()), 31)
        self.assertEqual(int(storage.getNode('image_width').real()), 1280)
        self.assertEqual(int(storage.getNode('image_height').real()), 720)
        storage.release()
        images = list((folder / 'calib_images_11x8').glob('*.jpg'))
        self.assertEqual(len(images), 31)
        self.assertTrue(all(cv2.imread(str(p)).shape[:2] == (720, 1280) for p in images))
        marker = cv2.imread(str(folder / 'aruco_4x4_id0.png'))
        detector = cv2.aruco.ArucoDetector(cv2.aruco.getPredefinedDictionary(cv2.aruco.DICT_4X4_50))
        _, ids, _ = detector.detectMarkers(marker)
        self.assertIsNotNone(ids)
        self.assertIn(0, ids.ravel())


if __name__ == '__main__':
    unittest.main()
