"""Regenerate the short synthetic audio fixtures using a full FFmpeg CLI."""

from pathlib import Path
import subprocess


def main():
    destination = Path(__file__).resolve().parents[1] / "test/data/audio"
    destination.mkdir(parents=True, exist_ok=True)
    formats = [
        ("pcm16.wav", "pcm_s16le"),
        ("pcm24.wav", "pcm_s24le"),
        ("float.wav", "pcm_f32le"),
        ("pcm.aiff", "pcm_s16be"),
        ("audio.mp3", "libmp3lame"),
        ("audio.flac", "flac"),
        ("audio.aac", "aac"),
        ("audio.m4a", "aac"),
        ("vorbis.ogg", "vorbis"),
        ("opus.ogg", "libopus"),
        ("opus.webm", "libopus"),
    ]
    for filename, codec in formats:
        subprocess.run(
            [
                "ffmpeg",
                "-hide_banner",
                "-loglevel",
                "error",
                "-y",
                "-f",
                "lavfi",
                "-i",
                "sine=frequency=440:sample_rate=48000:duration=0.25",
                "-ac",
                "2",
                "-c:a",
                codec,
                "-strict",
                "-2",
                "-fflags",
                "+bitexact",
                "-flags:a",
                "+bitexact",
                "-map_metadata",
                "-1",
                str(destination / filename),
            ],
            check=True,
        )


if __name__ == "__main__":
    main()
