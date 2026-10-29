"""
Model Training and Rigorous Evaluation for SupplyChainX 2.0 Demand Forecasting.
Compares:
  - Baseline A: Seasonal Naive (d_hat_{t+1} = d_{t-6})
  - Baseline B: Linear Regression
  - Model C: HistGradientBoostingRegressor (Gradient Boosted Trees)

Evaluates on held-out test scenarios (split by scenario ID, preventing time-series leakage),
measuring MAE and RMSE separately under normal and demand-surge conditions.
"""

import json
import math
import os
import pickle
from typing import Dict, List, Tuple

import numpy as np
from sklearn.linear_model import LinearRegression
from sklearn.ensemble import HistGradientBoostingRegressor
from sklearn.metrics import mean_absolute_error, root_mean_squared_error


FEATURE_COLS = [
    "demand_lag_1",
    "demand_lag_7",
    "rolling_mean_7",
    "rolling_std_7",
    "day_of_week",
    "surge_active",
    "customer_baseline",
]


def load_dataset(path: str = "data/ml_demand_dataset.json") -> List[Dict]:
    if not os.path.exists(path):
        from generate_data import generate_dataset
        return generate_dataset(num_scenarios=80, out_path=path)
    with open(path, "r") as f:
        return json.load(f)


def prepare_matrices(records: List[Dict]) -> Tuple[np.ndarray, np.ndarray, np.ndarray, np.ndarray]:
    X = np.array([[r[col] for col in FEATURE_COLS] for r in records], dtype=float)
    y = np.array([r["target_demand_t1"] for r in records], dtype=float)
    y_naive = np.array([r["demand_lag_7"] for r in records], dtype=float)
    is_surge = np.array([r["is_next_day_surge"] for r in records], dtype=bool)
    return X, y, y_naive, is_surge


def evaluate_predictions(y_true: np.ndarray, y_pred: np.ndarray, is_surge: np.ndarray) -> Dict[str, float]:
    mae_all = float(mean_absolute_error(y_true, y_pred))
    rmse_all = float(root_mean_squared_error(y_true, y_pred))
    
    # Normal days vs surge days
    normal_mask = ~is_surge
    mae_normal = float(mean_absolute_error(y_true[normal_mask], y_pred[normal_mask]))
    rmse_normal = float(root_mean_squared_error(y_true[normal_mask], y_pred[normal_mask]))
    
    if np.any(is_surge):
        mae_surge = float(mean_absolute_error(y_true[is_surge], y_pred[is_surge]))
        rmse_surge = float(root_mean_squared_error(y_true[is_surge], y_pred[is_surge]))
    else:
        mae_surge, rmse_surge = 0.0, 0.0

    return {
        "mae_overall": round(mae_all, 3),
        "rmse_overall": round(rmse_all, 3),
        "mae_normal": round(mae_normal, 3),
        "rmse_normal": round(rmse_normal, 3),
        "mae_surge": round(mae_surge, 3),
        "rmse_surge": round(rmse_surge, 3),
    }


def run_experiment():
    records = load_dataset()
    
    # Split by scenario ID: 70% train, 15% validation, 15% test
    scenario_ids = sorted(list(set(r["scenario_id"] for r in records)))
    n_scen = len(scenario_ids)
    n_train = int(0.70 * n_scen)
    n_val = int(0.15 * n_scen)
    
    train_ids = set(scenario_ids[:n_train])
    val_ids = set(scenario_ids[n_train : n_train + n_val])
    test_ids = set(scenario_ids[n_train + n_val:])
    
    train_recs = [r for r in records if r["scenario_id"] in train_ids]
    val_recs = [r for r in records if r["scenario_id"] in val_ids]
    test_recs = [r for r in records if r["scenario_id"] in test_ids]
    
    X_train, y_train, y_naive_train, is_surge_train = prepare_matrices(train_recs)
    X_val, y_val, y_naive_val, is_surge_val = prepare_matrices(val_recs)
    X_test, y_test, y_naive_test, is_surge_test = prepare_matrices(test_recs)
    
    print(f"Dataset split: Train={len(train_recs)}, Val={len(val_recs)}, Test={len(test_recs)}")
    
    # 1. Baseline A: Seasonal Naive
    eval_naive = evaluate_predictions(y_test, y_naive_test, is_surge_test)
    
    # 2. Baseline B: Linear Regression
    lr = LinearRegression()
    lr.fit(X_train, y_train)
    y_pred_lr = np.maximum(0.0, lr.predict(X_test))
    eval_lr = evaluate_predictions(y_test, y_pred_lr, is_surge_test)
    
    # 3. Model C: HistGradientBoostingRegressor
    gbt = HistGradientBoostingRegressor(
        max_iter=150,
        learning_rate=0.08,
        max_leaf_nodes=31,
        random_state=42
    )
    gbt.fit(X_train, y_train)
    y_pred_gbt = np.maximum(0.0, gbt.predict(X_test))
    eval_gbt = evaluate_predictions(y_test, y_pred_gbt, is_surge_test)
    
    print("\n" + "=" * 70)
    print("EXPERIMENTAL EVALUATION ON HELD-OUT SCENARIOS (TEST SET)")
    print("=" * 70)
    print(f"{'Model':<25} | {'Overall MAE':<12} | {'Overall RMSE':<12} | {'Normal MAE':<12} | {'Surge MAE':<12}")
    print("-" * 70)
    print(f"{'A: Seasonal Naive':<25} | {eval_naive['mae_overall']:<12} | {eval_naive['rmse_overall']:<12} | {eval_naive['mae_normal']:<12} | {eval_naive['mae_surge']:<12}")
    print(f"{'B: Linear Regression':<25} | {eval_lr['mae_overall']:<12} | {eval_lr['rmse_overall']:<12} | {eval_lr['mae_normal']:<12} | {eval_lr['mae_surge']:<12}")
    print(f"{'C: Gradient-Boosted Trees':<25} | {eval_gbt['mae_overall']:<12} | {eval_gbt['rmse_overall']:<12} | {eval_gbt['mae_normal']:<12} | {eval_gbt['mae_surge']:<12}")
    print("=" * 70)
    
    # Save the trained model and feature metadata
    os.makedirs("ml/artifacts", exist_ok=True)
    with open("ml/artifacts/demand_forecaster.pkl", "wb") as f:
        pickle.dump({
            "model": gbt,
            "feature_cols": FEATURE_COLS,
            "metrics": {
                "naive": eval_naive,
                "linear_regression": eval_lr,
                "gbt": eval_gbt,
            }
        }, f)
    print("Saved trained forecaster artifact to ml/artifacts/demand_forecaster.pkl")


if __name__ == "__main__":
    run_experiment()

