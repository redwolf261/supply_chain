"""
Fast local HTTP microservice for SupplyChainX 2.0 ML demand forecasting.
Exposes:
  - GET  /health: returns model metadata and validation metrics
  - POST /predict: takes customer lag features, returns next-day demand point forecasts
"""

import http.server
import json
import os
import pickle
import sys
from typing import Dict, List
import numpy as np

ARTIFACT_PATH = os.path.join(os.path.dirname(__file__), "artifacts", "demand_forecaster.pkl")

model_artifact = None

def get_artifact():
    global model_artifact
    if model_artifact is None:
        if not os.path.exists(ARTIFACT_PATH):
            from train_and_evaluate import run_experiment
            run_experiment()
        with open(ARTIFACT_PATH, "rb") as f:
            model_artifact = pickle.load(f)
    return model_artifact


class ForecastHandler(http.server.BaseHTTPRequestHandler):
    def _send(self, status: int, data: Dict):
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Access-Control-Allow-Origin", "*")
        self.end_headers()
        self.wfile.write(json.dumps(data).encode("utf-8"))

    def do_GET(self):
        if self.path == "/health":
            art = get_artifact()
            self._send(200, {
                "status": "ok",
                "model": "HistGradientBoostingRegressor",
                "features": art["feature_cols"],
                "metrics": art["metrics"]
            })
        else:
            self._send(404, {"error": "not_found"})

    def do_POST(self):
        if self.path == "/predict":
            try:
                length = int(self.headers.get("Content-Length", 0))
                body = json.loads(self.rfile.read(length).decode("utf-8"))
                art = get_artifact()
                model = art["model"]
                cols = art["feature_cols"]

                # Expected input: {"customers": [{"customer_id": "C1", "demand_lag_1": 40, ...}]}
                items = body.get("customers", [])
                if not items:
                    self._send(400, {"error": "missing 'customers' list"})
                    return

                X = []
                for item in items:
                    row = [float(item.get(col, 0.0)) for col in cols]
                    X.append(row)

                predictions = model.predict(np.array(X))
                results = []
                for idx, item in enumerate(items):
                    val = max(0, int(round(predictions[idx])))
                    results.append({
                        "customer_id": item.get("customer_id", f"C{idx+1}"),
                        "forecast_demand": val
                    })

                self._send(200, {"forecasts": results})
            except Exception as e:
                self._send(500, {"error": str(e)})
        else:
            self._send(404, {"error": "not_found"})


def run_service(port: int = 5050):
    server = http.server.HTTPServer(("127.0.0.1", port), ForecastHandler)
    print(f"ML Forecast Service listening on http://127.0.0.1:{port}")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 5050
    run_service(port)

