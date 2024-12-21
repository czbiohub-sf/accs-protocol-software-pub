import collections
import requests


class CciError(Exception):
    pass


class CciClient:
    def __init__(self, server_url: str):
        self.server_url = server_url

    def _post_request(self, action: str):
        try:
            r = requests.post(
                self.server_url, json={'Action': action}, timeout=180.)
        except Exception as e:
            raise CciError(f"HTTP request failed: {e}") from e
        if r.status_code >= 400:
            try:
                info = r.json()
                msg = info["error"]
            except Exception as e:
                msg = None
            if msg is None:
                raise CciError(
                    f"Unknown server error (status_code={r.status_code})")
            else:
                raise CciError(f"Server returned error: {msg}")
        try:
            return r.json()
        except Exception as e:
            raise CciError(f"JSON response parsing failed: {e}") from e

    def do_bg_scan(self):
        try:
            self._post_request("Start Background")
        except Exception as e:
            raise CciError(f"Background scan request failed: {e}") from e

    def start_new_record(self):
        try:
            self._post_request("Open CSV")
        except Exception as e:
            raise CciError(f"'Open CSV' request failed: {e}") from e

    def do_counting_scan(self):
        info = self._post_request("Start OT2 Scan")
        if "error" in info:
            raise CciError(f"Server returned error: {info['error']}")
        try:
            densities = info['Status']['Scan Results']
        except Exception as e:
            raise CciError("Response missing cell counts")
        try:
            valid_areas = info['Status']['valid_areas']
        except Exception as e:
            valid_areas = None
        try:
            warnings = info['Status']['warnings']
        except Exception as e:
            warnings = None
        try:
            diag_info = info['Status']['diag_info']
        except Exception as e:
            diag_info = None
        try:
            densities = [float(x) for x in densities]
        except Exception as e:
            raise CciError("Invalid density data")
        if valid_areas is not None:
            try:
                valid_areas = [float(x) for x in valid_areas]
            except Exception as e:
                raise CciError("Invalid valid area data")
        if warnings is not None:
            try:
                warnings = [
                    [str(text) for text in lane_warnings]
                    for lane_warnings in warnings]
            except Exception as e:
                raise CciError("Invalid warnings data")

        return densities, valid_areas, warnings


class DummyCciClient(CciClient):
    def __init__(self, *args, **kwargs):
        self._densities = collections.deque([
             12.34567, 123.45670, 234.56701, 345.67012,
            456.70123, 567.01234, 670.12345, 701.23456])

    def start_new_record(self):
        pass

    def do_bg_scan(self):
        pass

    def do_counting_scan(self):
        densities = list(self._densities)
        valid_areas = [1.0]*len(densities)
        warnings = []
        #self._densities.rotate(3)
        return densities, valid_areas, warnings
