# KVS

## Device uplink env vars (`kvs_server`)

`kvs_server/kvs_service.c` reads the following environment variables for KVS uplink:

- `AWS_REGION` (required)
- `AWS_ACCESS_KEY_ID` (required)
- `AWS_SECRET_ACCESS_KEY` (required)
- `AWS_SESSION_TOKEN` (optional, recommended for short-lived credentials)
- `KVS_CERT_PATH` (optional)
- `KVS_PRIVATE_KEY_PATH` (optional)
- `KVS_CA_CERT_PATH` (optional)
- `KVS_STREAM_PREFIX` (optional, default `event`)
- `KVS_EVENT_INTERVAL_MS` (optional, default `3000`)
- `KVS_EVENT_DURATION_MS` (optional, default `10000`)

For each simulated event trigger, device side creates a unique stream name:
`<KVS_STREAM_PREFIX>-<unix_ms>-<sequence>` and performs `CreateStream` before pushing frames.
