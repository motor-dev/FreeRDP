# FreeRDP: A Remote Desktop Protocol Implementation
# FreeRDP cmake build script
#
# Copyright 2019 Armin Novak <armin.novak@thincast.com>
# Copyright 2019 Thincast Technologies GmbH
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

set(SRCS urbdrc_types.h urbdrc_helpers.h urbdrc_helpers.c msusb.h msusb.c)

add_library(urbdrc-common STATIC ${SRCS})
set_property(TARGET urbdrc-common PROPERTY FOLDER "Channels/${CHANNEL_NAME}/Common")

channel_install(urbdrc-common ${FREERDP_ADDIN_PATH} "FreeRDPTargets")
