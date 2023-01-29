#ifndef MESH_LOG_H
#define MESH_LOG_H

#define MAX_LOG_LEVEL ESP_LOG_DEBUG
#define LOG_NAME "mn"

#define FORMAT_LOG_MSG(format, letter) "(" __TIME__ ") (" #letter ") " LOG_NAME ": " format

#define LOGL__(level, format, ...) do { \
    if (MAX_LOG_LEVEL >= level) esp_log_write(level, LOG_NAME, format, ##__VA_ARGS__); \
} while(0)

#define LOGD__(format, ...) LOGL__(ESP_LOG_DEBUG, format, ##__VA_ARGS__)
#define LOGI__(format, ...) LOGL__(ESP_LOG_INFO, format, ##__VA_ARGS__)
#define LOGW__(format, ...) LOGL__(ESP_LOG_WARN, format, ##__VA_ARGS__)
#define LOGE__(format, ...) LOGL__(ESP_LOG_ERROR, format, ##__VA_ARGS__)

#define LOGD_(format, ...) LOGD__((FORMAT_LOG_MSG(format, D)), ##__VA_ARGS__)
#define LOGI_(format, ...) LOGI__((FORMAT_LOG_MSG(format, I)), ##__VA_ARGS__)
#define LOGW_(format, ...) LOGW__((FORMAT_LOG_MSG(format, W)), ##__VA_ARGS__)
#define LOGE_(format, ...) LOGE__((FORMAT_LOG_MSG(format, E)), ##__VA_ARGS__)

#define LOGD(format, ... ) LOGD_(format "\n", ##__VA_ARGS__)
#define LOGI(format, ... ) LOGI_(format "\n", ##__VA_ARGS__)
#define LOGW(format, ... ) LOGW_(format "\n", ##__VA_ARGS__)
#define LOGE(format, ... ) LOGE_(format "\n", ##__VA_ARGS__)

#endif //MESH_LOG_H
