#pragma once

namespace CMD {
    enum Type : unsigned short {
        CMD_TEST_CONNECT = 100,
        CMD_LIST_DRIVES  = 101,
        CMD_LIST_DIR     = 102,

        CMD_DOWNLOAD_START = 103,
        CMD_DOWNLOAD_CHUNK = 104,
        CMD_DOWNLOAD_END   = 105,
        
        CMD_ERROR        = 999
    };
}