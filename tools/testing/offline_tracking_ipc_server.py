#!/usr/bin/env python3
import argparse
import os
import pathlib
import socket
import struct
import sys

MAGIC = 0xDEADBEEF


def parse_args():
    repo_root = pathlib.Path(__file__).resolve().parents[2]
    parser = argparse.ArgumentParser(description='Offline tracking IPC test server for media_agent')
    parser.add_argument('--proto-dir', default=str(repo_root / 'tools' / 'testing' / 'proto_py'))
    parser.add_argument('--socket-path', default='/tmp/media_agent.sock')
    parser.add_argument('--agent-id', default='agent_001')
    parser.add_argument('--stream-id', default='offline_track_001')
    parser.add_argument('--rtsp-url', default='rtsp://127.0.0.1:8554/offline_tracking')
    parser.add_argument('--new-rtsp-url', default='')
    parser.add_argument('--model-path', default=str(repo_root / 'third_party' / 'edgeInfer' / 'weights' / 'hardhat_detect_yolov8s.rknn'))
    parser.add_argument('--algorithm-id', default='hardhat_detect')
    parser.add_argument('--threshold', type=float, default=0.25)
    parser.add_argument('--alarm-level', type=int, default=2)
    parser.add_argument('--reconnect-interval-s', type=int, default=3)
    parser.add_argument('--snapshot-dir', default=str(repo_root / 'test_output' / 'snapshots'))
    parser.add_argument('--record-dir', default=str(repo_root / 'test_output' / 'records'))
    parser.add_argument('--record-duration-s', type=int, default=10)
    parser.add_argument('--tracker-enabled', action='store_true', default=True)
    parser.add_argument('--tracker-type', default='bytetrack')
    parser.add_argument('--min-thresh', type=float, default=0.1)
    parser.add_argument('--high-thresh', type=float, default=0.5)
    parser.add_argument('--max-iou-distance', type=float, default=0.7)
    parser.add_argument('--high-thresh-person', type=float, default=0.4)
    parser.add_argument('--high-thresh-motorbike', type=float, default=0.4)
    parser.add_argument('--max-age', type=int, default=70)
    parser.add_argument('--n-init', type=int, default=3)
    return parser.parse_args()


def load_proto_modules(proto_dir):
    sys.path.insert(0, proto_dir)
    try:
        import media_agent_pb2  # type: ignore
        import types_pb2  # type: ignore
        import version_pb2  # type: ignore
    except ImportError as exc:
        raise SystemExit(
            'Failed to import protobuf Python modules from {}. Run ./tools/testing/generate_proto_py.sh first. Error: {}'.format(proto_dir, exc)
        )
    return media_agent_pb2, types_pb2, version_pb2


def recv_exact(conn, size):
    chunks = []
    remaining = size
    while remaining > 0:
        chunk = conn.recv(remaining)
        if not chunk:
            return None
        chunks.append(chunk)
        remaining -= len(chunk)
    return b''.join(chunks)


def recv_frame(conn):
    header = recv_exact(conn, 8)
    if header is None:
        return None
    magic, length = struct.unpack('!II', header)
    if magic != MAGIC:
        raise RuntimeError('Bad magic: 0x{:08X}'.format(magic))
    payload = recv_exact(conn, length)
    if payload is None:
        return None
    return payload


def send_frame(conn, payload):
    conn.sendall(struct.pack('!II', MAGIC, len(payload)) + payload)


def safe_detection_type_name(types_pb2, value):
    try:
        return types_pb2.DetectionType.Name(value)
    except Exception:
        return str(value)


def build_config(args, media_agent_pb2, version_pb2):
    env = media_agent_pb2.Envelope()
    env.version = version_pb2.PROTO_VERSION_CURRENT
    env.type = media_agent_pb2.MSG_CONFIG
    env.seq = 1

    cfg = env.config
    cfg.agent_id = args.agent_id

    stream = cfg.streams.add()
    stream.enabled = True
    stream.stream_id = args.stream_id
    stream.rtsp_url = args.rtsp_url
    stream.new_rtsp_url = args.new_rtsp_url
    stream.reconnect_interval_s = args.reconnect_interval_s
    stream.alarm_snapshot_dir = args.snapshot_dir
    stream.alarm_record_dir = args.record_dir
    stream.alarm_record_duration_s = args.record_duration_s

    algo = stream.algorithms.add()
    algo.algorithm_id = args.algorithm_id
    algo.model_path = args.model_path
    algo.threshold = args.threshold
    algo.alarm_level = args.alarm_level
    algo.start_date = 0
    algo.end_date = 0

    tracker = stream.tracker
    tracker.enabled = args.tracker_enabled
    tracker.tracker_type = args.tracker_type
    tracker.min_thresh = args.min_thresh
    tracker.high_thresh = args.high_thresh
    tracker.max_iou_distance = args.max_iou_distance
    tracker.high_thresh_person = args.high_thresh_person
    tracker.high_thresh_motorbike = args.high_thresh_motorbike
    tracker.max_age = args.max_age
    tracker.n_init = args.n_init
    return env.SerializeToString()


def handle_envelope(env, media_agent_pb2, types_pb2):
    if env.type == media_agent_pb2.MSG_CONFIG_ACK:
        print('[CONFIG_ACK] agent_id={} success={} message={}'.format(
            env.config_ack.agent_id,
            env.config_ack.success,
            env.config_ack.message,
        ))
        return

    if env.type == media_agent_pb2.MSG_HEARTBEAT:
        print('[HEARTBEAT] agent_id={} uptime_s={} stream_count={}'.format(
            env.heartbeat.agent_id,
            env.heartbeat.uptime_s,
            env.heartbeat.stream_count,
        ))
        return

    if env.type == media_agent_pb2.MSG_ALARM:
        alarm = env.alarm
        target = alarm.target
        bbox = target.bbox
        print('[ALARM] stream_id={} alarm_type={} level={} object_id={} type={} conf={:.3f} bbox=({:.3f},{:.3f},{:.3f},{:.3f}) snapshot={} record={}'.format(
            alarm.stream_id,
            alarm.alarm_type,
            alarm.level,
            target.object_id,
            safe_detection_type_name(types_pb2, target.type),
            target.confidence,
            bbox.x,
            bbox.y,
            bbox.width,
            bbox.height,
            alarm.snapshot_name,
            alarm.record_name,
        ))
        return

    print('[RECV] type={} seq={}'.format(env.type, env.seq))


def main():
    args = parse_args()
    media_agent_pb2, types_pb2, version_pb2 = load_proto_modules(args.proto_dir)

    pathlib.Path(args.snapshot_dir).mkdir(parents=True, exist_ok=True)
    pathlib.Path(args.record_dir).mkdir(parents=True, exist_ok=True)

    if os.path.exists(args.socket_path):
        os.unlink(args.socket_path)

    server = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    server.bind(args.socket_path)
    server.listen(1)

    print('[INFO] listening on {}'.format(args.socket_path))
    print('[INFO] waiting for media_agent to connect...')
    print('[INFO] stream_id={} rtsp_url={} model_path={}'.format(
        args.stream_id,
        args.rtsp_url,
        args.model_path,
    ))

    conn = None
    try:
        conn, _ = server.accept()
        print('[INFO] media_agent connected')
        payload = build_config(args, media_agent_pb2, version_pb2)
        send_frame(conn, payload)
        print('[SEND] MSG_CONFIG sent, tracker_enabled={} tracker_type={} n_init={}'.format(
            args.tracker_enabled,
            args.tracker_type,
            args.n_init,
        ))

        while True:
            payload = recv_frame(conn)
            if payload is None:
                print('[INFO] peer disconnected')
                break
            env = media_agent_pb2.Envelope()
            env.ParseFromString(payload)
            handle_envelope(env, media_agent_pb2, types_pb2)
    except KeyboardInterrupt:
        print('\n[INFO] interrupted by user')
    finally:
        try:
            if conn is not None:
                conn.close()
        finally:
            server.close()
            if os.path.exists(args.socket_path):
                os.unlink(args.socket_path)
            print('[INFO] socket server stopped')


if __name__ == '__main__':
    main()
