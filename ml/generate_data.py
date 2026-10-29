"""
Synthetic Scenario Data Generator for SupplyChainX 2.0 Demand Forecasting.
Generates multi-customer, multi-day demand histories with:
  - Customer-specific baselines
  - Weekly seasonality (e.g. weekday vs weekend / day-of-week demand multipliers)
  - Autoregressive continuity (lagged dependence)
  - Random Gaussian noise
  - Rare demand surges (unanticipated spikes)

Outputs a structured tabular dataset of lag features, rolling statistics,
and next-day demand targets without data leakage.
"""

import argparse
import json
import math
import os
import random
from typing import Dict, List, Tuple


def generate_scenario_history(
    scenario_id: int,
    horizon_days: int = 45,
    num_customers: int = 6,
    surge_probability: float = 0.05,
    seed: int = 42
) -> List[Dict]:
    rng = random.Random(seed + scenario_id * 1000)
    
    # 1. Customer profiles: baseline demand and variability
    customers = []
    for c_id in range(num_customers):
        baseline = rng.randint(30, 90)
        volatility = rng.uniform(0.10, 0.25)
        # Unique weekly seasonality profile per customer
        seasonality = [
            1.0 + 0.25 * math.sin(2.0 * math.pi * d / 7.0 + (c_id * 0.5))
            for d in range(7)
        ]
        customers.append({
            "id": f"C{c_id + 1}",
            "baseline": baseline,
            "volatility": volatility,
            "seasonality": seasonality
        })
    
    # 2. Simulate daily demand series
    daily_demands: Dict[str, List[int]] = {c["id"]: [] for c in customers}
    daily_surges: Dict[str, List[int]] = {c["id"]: [] for c in customers}

    for day in range(horizon_days):
        dow = day % 7
        for c in customers:
            cid = c["id"]
            seasonal_factor = c["seasonality"][dow]
            base_val = c["baseline"] * seasonal_factor
            
            # Autoregressive component from yesterday if available
            if day > 0:
                prev = daily_demands[cid][-1]
                target_mean = 0.7 * base_val + 0.3 * prev
            else:
                target_mean = base_val
            
            noise = rng.gauss(0, c["volatility"] * c["baseline"])
            raw_val = max(5, int(round(target_mean + noise)))
            
            # Check for rare demand surge
            is_surge = 1 if rng.random() < surge_probability else 0
            if is_surge:
                raw_val = int(round(raw_val * rng.uniform(1.5, 2.3)))
                
            daily_demands[cid].append(raw_val)
            daily_surges[cid].append(is_surge)

    # 3. Construct feature vectors without future leakage
    # We require at least 8 days of history to compute lag_7 and rolling_7 stats
    records = []
    for c in customers:
        cid = c["id"]
        demands = daily_demands[cid]
        surges = daily_surges[cid]
        
        for t in range(7, horizon_days - 1):
            target_next_day = demands[t + 1]
            lag_1 = demands[t]
            lag_7 = demands[t - 6]
            window_7 = demands[t - 6 : t + 1]
            rolling_mean_7 = sum(window_7) / 7.0
            variance = sum((x - rolling_mean_7) ** 2 for x in window_7) / 7.0
            rolling_std_7 = math.sqrt(variance)
            dow = (t + 1) % 7
            surge_active = surges[t]
            
            records.append({
                "scenario_id": scenario_id,
                "day": t,
                "customer_id": cid,
                "customer_baseline": c["baseline"],
                "day_of_week": dow,
                "demand_lag_1": lag_1,
                "demand_lag_7": lag_7,
                "rolling_mean_7": round(rolling_mean_7, 2),
                "rolling_std_7": round(rolling_std_7, 2),
                "surge_active": surge_active,
                "target_demand_t1": target_next_day,
                "is_next_day_surge": surges[t + 1]
            })

    return records


def generate_dataset(num_scenarios: int = 80, out_path: str = "data/ml_demand_dataset.json"):
    os.makedirs(os.path.dirname(out_path), exist_ok=True)
    all_records = []
    for s in range(num_scenarios):
        scen_records = generate_scenario_history(scenario_id=s, seed=100 + s)
        all_records.extend(scen_records)
        
    with open(out_path, "w") as f:
        json.dump(all_records, f, indent=2)
    print(f"Generated {len(all_records)} customer-day observations across {num_scenarios} scenarios to {out_path}.")
    return all_records


if __name__ == "__main__":
    generate_dataset()

