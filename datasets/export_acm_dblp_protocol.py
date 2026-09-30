"""
Export DBLP-ACM numeric vectors and normalized record text for the C++ harness.

This uses DBLP as Server and ACM as Client. The legacy numeric client labels
use nearest-DBLP L2 distance. The separately exported gold protocols use the
benchmark's DBLP-ACM mapping: E2LSH hashes the same 28-D vectors while OMH
hashes normalized original record text.
"""

import json
import html
import re
import unicodedata
from pathlib import Path

import numpy as np

from acm_dblp import URL, _Scaler, download_zip, extract_features, load_tables


OUT_DIR = Path("datasets/acm_dblp_protocol")
L2_CLOSE_THRESHOLD = 1.2


def _write_json(path: Path, value) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w") as out:
        json.dump(value, out, separators=(",", ":"))


def _nearest_server_indices(server_vectors: np.ndarray, client_vectors: np.ndarray):
    server = server_vectors.astype(np.float64)
    client = client_vectors.astype(np.float64)
    nearest_idx = np.empty(len(client), dtype=np.int64)
    nearest_dist = np.empty(len(client), dtype=np.float64)

    for start in range(0, len(client), 8):
        chunk = client[start:start + 8]
        diff = chunk[:, None, :] - server[None, :, :]
        distances_sq = np.sum(diff * diff, axis=2)
        idx = np.argmin(distances_sq, axis=1)
        nearest_idx[start:start + len(chunk)] = idx
        nearest_dist[start:start + len(chunk)] = np.sqrt(
            distances_sq[np.arange(len(chunk)), idx]
        )

    return nearest_idx, nearest_dist


def _normalise_text(value: str) -> str:
    value = html.unescape(value or "")
    value = unicodedata.normalize("NFKC", value).casefold()
    return re.sub(r"\s+", " ", value).strip()


def _record_text(row: dict) -> str:
    """Canonical text sequence used by the edit-distance/OMH experiment."""
    return " | ".join((
        _normalise_text(row.get("title", "")),
        _normalise_text(row.get("year", "")),
    ))


def _edit_distance(left: str, right: str) -> int:
    if len(left) < len(right):
        left, right = right, left
    previous = list(range(len(right) + 1))
    for i, left_char in enumerate(left, 1):
        current = [i]
        for j, right_char in enumerate(right, 1):
            current.append(min(
                current[-1] + 1,
                previous[j] + 1,
                previous[j - 1] + (left_char != right_char),
            ))
        previous = current
    return previous[-1]


def _export_gold_protocol(
    rows_dblp,
    rows_acm,
    gold,
    dblp_key,
    acm_key,
    dblp_vectors,
    acm_vectors,
) -> None:
    """Export text and 28-D inputs with the same gold-standard match labels."""
    dblp_by_id = {row[dblp_key]: row for row in rows_dblp}
    acm_by_id = {row[acm_key]: row for row in rows_acm}
    dblp_vector_by_id = {
        row[dblp_key]: dblp_vectors[i].astype(float).tolist()
        for i, row in enumerate(rows_dblp)
    }
    acm_vector_by_id = {
        row[acm_key]: acm_vectors[i].astype(float).tolist()
        for i, row in enumerate(rows_acm)
    }
    gold_columns = list(gold[0].keys())
    dblp_gold_key = next(key for key in gold_columns if "dblp" in key.lower())
    acm_gold_key = next(key for key in gold_columns if "acm" in key.lower())

    server_text = {
        row[dblp_key]: _record_text(row)
        for row in rows_dblp
    }
    server_gold = dict(dblp_vector_by_id)
    close_text = {}
    close_gold = {}
    matched_acm_ids = set()
    for pair in gold:
        dblp_id = pair[dblp_gold_key]
        acm_id = pair[acm_gold_key]
        if dblp_id not in dblp_by_id or acm_id not in acm_by_id:
            continue
        matched_acm_ids.add(acm_id)
        server_record = server_text[dblp_id]
        client_record = _record_text(acm_by_id[acm_id])
        normalizer = max(len(server_record), len(client_record), 1)
        distance = _edit_distance(server_record, client_record) / normalizer
        close_text.setdefault(dblp_id, {"close": []})["close"].append({
            "train_id": acm_id,
            "distance": distance,
            "text": client_record,
        })
        close_gold.setdefault(dblp_id, {"close": []})["close"].append({
            "train_id": acm_id,
            "distance": distance,
            "pixels": acm_vector_by_id[acm_id],
            "text": client_record,
        })

    far_text = {
        row[acm_key]: {
            "distance": 1.0,
            "text": _record_text(row),
        }
        for row in rows_acm
        if row[acm_key] not in matched_acm_ids
    }
    far_gold = {
        row[acm_key]: {
            "distance": 1.0,
            "pixels": acm_vector_by_id[row[acm_key]],
            "text": _record_text(row),
        }
        for row in rows_acm
        if row[acm_key] not in matched_acm_ids
    }

    _write_json(OUT_DIR / "server_text.json", server_text)
    _write_json(OUT_DIR / "client_close_text.json", close_text)
    _write_json(OUT_DIR / "client_far_text.json", far_text)
    (OUT_DIR / "metadata_text.txt").write_text(f"{len(server_text)}\n")
    _write_json(OUT_DIR / "server_gold.json", server_gold)
    _write_json(OUT_DIR / "client_close_gold.json", close_gold)
    _write_json(OUT_DIR / "client_far_gold.json", far_gold)
    (OUT_DIR / "metadata_gold.txt").write_text(f"{len(server_gold)}\n")
    print(f"text_server={len(server_text)}")
    print(f"text_close_pairs={sum(len(v['close']) for v in close_text.values())}")
    print(f"text_far={len(far_text)}")
    print(f"gold_vector_server={len(server_gold)}")
    print(f"gold_vector_close_pairs={sum(len(v['close']) for v in close_gold.values())}")
    print(f"gold_vector_far={len(far_gold)}")


def main() -> None:
    zf = download_zip(URL)
    rows_dblp, rows_acm, _gold = load_tables(zf)

    def id_key(rows, label):
        for key in rows[0]:
            if key.strip().lower() == "id":
                return key
        raise KeyError(f"{label}: no 'id' column found")

    dblp_key = id_key(rows_dblp, "DBLP")
    acm_key = id_key(rows_acm, "ACM")

    scaler = _Scaler()
    dblp_vectors = scaler.fit_transform(extract_features(rows_dblp))
    acm_vectors = scaler.transform(extract_features(rows_acm))

    server_ids = [row[dblp_key] for row in rows_dblp]
    server = {
        server_id: dblp_vectors[i].astype(float).tolist()
        for i, server_id in enumerate(server_ids)
    }

    nearest_idx, nearest_dist = _nearest_server_indices(dblp_vectors, acm_vectors)
    close = {}
    far = {}

    for i, row in enumerate(rows_acm):
        acm_id = row[acm_key]
        distance = float(nearest_dist[i])
        pixels = acm_vectors[i].astype(float).tolist()
        if distance <= L2_CLOSE_THRESHOLD:
            server_id = server_ids[int(nearest_idx[i])]
            close.setdefault(server_id, {"close": []})["close"].append({
                "train_id": acm_id,
                "distance": distance,
                "pixels": pixels,
            })
        else:
            far[acm_id] = {
                "distance": distance,
                "pixels": pixels,
            }

    _write_json(OUT_DIR / "server.json", server)
    _write_json(OUT_DIR / "client_close.json", close)
    _write_json(OUT_DIR / "client_far.json", far)
    (OUT_DIR / "metadata.txt").write_text(f"{len(server)}\n")
    _export_gold_protocol(
        rows_dblp,
        rows_acm,
        _gold,
        dblp_key,
        acm_key,
        dblp_vectors,
        acm_vectors,
    )

    print(f"dim={dblp_vectors.shape[1]}")
    print(f"l2_threshold={L2_CLOSE_THRESHOLD}")
    print(f"server={len(server)}")
    print(f"close_pairs={sum(len(v['close']) for v in close.values())}")
    print(f"far={len(far)}")
    print(f"out={OUT_DIR}")


if __name__ == "__main__":
    main()
