#ifndef FSP_CONFIG_H
#define FSP_CONFIG_H

/* Cấu hình dùng chung (C11). CRC32 đã chuyển sang fsp_crc32.h (SDD §9 #3). */

#define FSP_SERVER_PORT 8088u
#define FSP_BACKLOG 64

/* Giới hạn payload của 1 frame. Lớn hơn -> đóng kết nối (SDD §3.1).
 * 1 frame FILE_CHUNK đầy = 88 + 65536 byte vẫn nằm trong giới hạn. */
#define FSP_BUFFER_SIZE (128u * 1024u)

#define FSP_SOCKET_TIMEOUT_S 30

/* Độ dài tối đa của đường dẫn ảo (gồm '\0') */
#define FSP_PATH_MAX 512

#define FSP_DEFAULT_SERVER_STORAGE "./storage/server_root"
#define FSP_DEFAULT_CLIENT_STORAGE "./storage/client_downloads"

#endif /* FSP_CONFIG_H */
