from __future__ import annotations

import base64
from datetime import datetime
import io
import struct
from typing import Any, Dict, List

from flask import Flask, jsonify, render_template, request

app = Flask(__name__)

MAX_RECORDS = 20

records: List[Dict[str, Any]] = []
_next_id = 1


def _safe_b64_len(b64_text: str) -> int:
    if not b64_text:
        return 0
    return len(b64_text)


def _safe_pcm_bytes(b64_text: str) -> int:
    if not b64_text:
        return 0
    try:
        return len(base64.b64decode(b64_text, validate=False))
    except Exception:
        return 0


def _push_record(record: Dict[str, Any]) -> None:
    global _next_id
    record["id"] = _next_id
    _next_id += 1
    record["ts"] = datetime.utcnow().isoformat(timespec="seconds") + "Z"
    records.insert(0, record)
    if len(records) > MAX_RECORDS:
        records.pop()


def _wav_bytes(pcm: bytes, sample_rate: int, channels: int) -> bytes:
    bits_per_sample = 16
    byte_rate = sample_rate * channels * bits_per_sample // 8
    block_align = channels * bits_per_sample // 8
    data_size = len(pcm)
    riff_size = 36 + data_size

    header = io.BytesIO()
    header.write(b"RIFF")
    header.write(struct.pack("<I", riff_size))
    header.write(b"WAVE")
    header.write(b"fmt ")
    header.write(struct.pack("<I", 16))
    header.write(struct.pack("<H", 1))
    header.write(struct.pack("<H", channels))
    header.write(struct.pack("<I", sample_rate))
    header.write(struct.pack("<I", byte_rate))
    header.write(struct.pack("<H", block_align))
    header.write(struct.pack("<H", bits_per_sample))
    header.write(b"data")
    header.write(struct.pack("<I", data_size))

    return header.getvalue() + pcm


def _normalize_pcm(pcm: bytes, target_peak: int = 28000) -> bytes:
    if not pcm:
        return pcm
    count = len(pcm) // 2
    samples = struct.unpack("<%dh" % count, pcm[: count * 2])
    peak = max((abs(s) for s in samples), default=0)
    if peak == 0:
        return pcm
    gain = min(target_peak / peak, 8.0)
    scaled = [
        max(-32768, min(32767, int(s * gain)))
        for s in samples
    ]
    return struct.pack("<%dh" % count, *scaled)


@app.route("/")
def index() -> str:
    return render_template("index.html", records=records, max_records=MAX_RECORDS)


@app.route("/predict", methods=["POST"])
def predict() -> Any:
    sample_rate = request.headers.get("X-Sample-Rate")
    channels = request.headers.get("X-Channels")
    encoding = request.headers.get("X-Encoding")
    duration_ms = request.headers.get("X-Duration-Ms")

    pcm_bytes = len(request.data or b"")
    content_length = request.headers.get("Content-Length", "(missing)")
    app.logger.info(
        "PCM upload: content_length=%s bytes=%d rate=%s ch=%s enc=%s dur=%s",
        content_length,
        pcm_bytes,
        sample_rate,
        channels,
        encoding,
        duration_ms,
    )
    if pcm_bytes == 0:
        return jsonify({"error": "Empty body"}), 400

    record = {
        "sample_rate": sample_rate,
        "channels": channels,
        "encoding": encoding,
        "duration_ms": duration_ms,
        "pcm_bytes": pcm_bytes,
        "audio_b64_len": None,
        "pcm": request.data,
    }
    _push_record(record)

    # Dummy response for your device parser
    return jsonify({"label": "Normal", "confidence": 0.95})


@app.route("/audio/<int:rec_id>.wav")
def audio(rec_id: int) -> Any:
    record = next((r for r in records if r.get("id") == rec_id), None)
    if not record or not record.get("pcm"):
        return jsonify({"error": "Not found"}), 404

    try:
        sample_rate = int(record.get("sample_rate") or 4000)
        channels = int(record.get("channels") or 1)
    except ValueError:
        sample_rate = 4000
        channels = 1

    boosted = _normalize_pcm(record["pcm"])
    wav = _wav_bytes(boosted, sample_rate, channels)
    return app.response_class(wav, mimetype="audio/wav")


if __name__ == "__main__":
    # Large payloads: keep debug off and allow big uploads
    app.config["MAX_CONTENT_LENGTH"] = 50 * 1024 * 1024
    app.run(host="0.0.0.0", port=5000, debug=False)
