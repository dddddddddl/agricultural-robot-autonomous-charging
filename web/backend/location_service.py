import requests


def reverse_geocode(latitude, longitude):
    fallback = {
        "latitude": float(latitude),
        "longitude": float(longitude),
        "city": "",
        "province": "",
        "country": "",
        "display_name": f"Coordinates {float(latitude):.4f}, {float(longitude):.4f}",
    }
    try:
        response = requests.get(
            "https://nominatim.openstreetmap.org/reverse",
            params={"lat": latitude, "lon": longitude, "format": "jsonv2", "accept-language": "en-US"},
            headers={"User-Agent": "AutoChargeDashboard/2.0 (local smart charging project)"},
            timeout=8,
        )
        response.raise_for_status()
        payload = response.json()
        address = payload.get("address", {})
        fallback.update(
            {
                "city": address.get("city") or address.get("town") or address.get("county") or "",
                "province": address.get("state") or address.get("province") or "",
                "country": address.get("country") or "",
                "display_name": payload.get("display_name") or fallback["display_name"],
            }
        )
    except (requests.RequestException, ValueError, TypeError):
        pass
    return fallback
