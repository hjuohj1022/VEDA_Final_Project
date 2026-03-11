#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

struct RuntimeConfig {
    struct ControlTlsConfig {
        bool enabled = true;
        bool require_client_cert = true;
        std::string ca_file = "certs/mTLS/rootCA.crt";
        // Control-channel TLS cert/key pair.
        std::string cert_file = "certs/mTLS/server.crt";
        std::string key_file = "certs/mTLS/server.key";
        std::string ssl_dll = "libssl-1_1-x64.dll";
        std::string crypto_dll = "libcrypto-1_1-x64.dll";
    };

    int capture_buffer_size = 4;
    int open_timeout_ms = 10000;
    int read_timeout_ms = 10000;
    // RTSPS camera input keeps using the camera/client cert pair.
    std::string ffmpeg_capture_options =
        "rtsp_transport;tcp|tls_verify;1|ca_file;certs/RTSP/rootCA.crt|cert_file;certs/RTSP/cctv.crt|key_file;certs/RTSP/cctv.key|stimeout;10000000|rw_timeout;10000000";

    int pause_loop_sleep_ms = 15;
    int grab_retry_sleep_ms = 20;
    int grab_fail_log_every = 100;

    std::size_t metrics_window_frames = 120U;
    double metrics_log_interval_sec = 2.0;

    int preview_width = 640;
    int preview_height = 480;

    bool dump_point_cloud = false;
    uint64_t point_cloud_dump_every_n = 60;
    int point_cloud_dump_subsample = 2;
    float point_cloud_min_depth_m = 0.1f;
    float point_cloud_max_depth_m = 80.0f;

    int pc_stream_render_width = 480;
    int pc_stream_render_height = 360;
    int pc_stream_render_subsample = 4;

    int pc_gui_render_width = 640;
    int pc_gui_render_height = 480;
    int pc_gui_render_subsample = 3;
    bool show_point_cloud_window = true;

    int png_compression = 3;

    int server_listen_backlog = 5;
    ControlTlsConfig control_tls;
};

const RuntimeConfig& GetRuntimeConfig();
