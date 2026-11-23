#pragma once
#include "link_msg.h"

namespace brick {
int renderRequest(mt::link::Msg type, const uint8_t* data, int size, uint8_t* reply);
}
