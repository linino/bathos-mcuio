#ifndef __MQTT_PAL_H__
#define __MQTT_PAL_H__

/*
  MIT License

  Copyright(c) 2018 Liam Bindle

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files(the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions :

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

/**
 * @file
 * @brief Includes/supports the types/calls required by the MQTT-C client.
 * 
 * @note This is the only file included in mqtt.h, and mqtt.c. It is therefore 
 *       responsible for including/supporting all the required types and calls. 
 * 
 * @defgroup pal Platform abstraction layer
 * @brief Documentation of the types and calls required to port MQTT-C to a
 *        new platform.
 * 
 * mqtt_pal.h is the only header file included in mqtt.c. Therefore, to port
 * MQTT-C to a  new platform the following types, functions, constants, and
 * macros must be defined in 
 * mqtt_pal.h:
 *  - Types:
 *      - size_t, ssize_t
 *      - uint8_t, uint16_t, uint32_t
 *      - va_list
 *      - mqtt_pal_time_t : return type of MQTT_PAL_TIME() 
 *      - mqtt_pal_mutex_t : type of the argument that is passed to
 *        MQTT_PAL_MUTEX_LOCK and MQTT_PAL_MUTEX_RELEASE
 *  - Functions:
 *      - memcpy, strlen
 *      - va_start, va_arg, va_end
 *  - Constants:
 *      - INT_MIN
 * 
 * Additionally, three macro's are required:
 *  - MQTT_PAL_HTONS(s) : host-to-network endian conversion for uint16_t.
 *  - MQTT_PAL_NTOHS(s) : network-to-host endian conversion for uint16_t.
 *  - MQTT_PAL_TIME()   : returns [type: mqtt_pal_time_t] current time in
 *                        seconds. 
 *  - MQTT_PAL_MUTEX_LOCK(mtx_pointer) : macro that locks the mutex pointed
 *                                       to by \c mtx_pointer.
 *  - MQTT_PAL_MUTEX_RELEASE(mtx_pointer) : macro that unlocks the mutex
 *                                          pointed to by mtx_pointer.
 * 
 * Lastly, mqtt_pal_sendall and mqtt_pal_recvall, must be implemented in
 * mqtt_pal.c for sending and receiving data using the platforms socket calls.
 */

#include <stdint.h>
#include <stdarg.h>
#include <limits.h>
#include <bathos/string.h>
#include <bathos/stdio.h>
#include <bathos/jiffies.h>

typedef unsigned int size_t;
typedef int ssize_t;
typedef struct bathos_pipe *mqtt_pal_socket_handle;
typedef unsigned long mqtt_pal_time_t;
typedef struct {
} mqtt_pal_mutex_t;

static inline void MQTT_PAL_MUTEX_INIT(mqtt_pal_mutex_t *m)
{
}

static inline void MQTT_PAL_MUTEX_LOCK(mqtt_pal_mutex_t *m)
{
}

static inline void MQTT_PAL_MUTEX_UNLOCK(mqtt_pal_mutex_t *m)
{
}

static inline mqtt_pal_time_t MQTT_PAL_TIME(void)
{
	return jiffies;
}

static inline uint16_t MQTT_PAL_NTOHS(uint16_t n)
{
	/* FIX THIS */
	return n;
}

static inline uint16_t MQTT_PAL_HTONS(uint16_t n)
{
	/* FIX THIS */
	return n;
}

/**
 * @brief Sends all the bytes in a buffer.
 * @ingroup pal
 * 
 * @param[in] fd The file-descriptor (or handle) of the socket.
 * @param[in] buf A pointer to the first byte in the buffer to send.
 * @param[in] len The number of bytes to send (starting at buf).
 * @param[in] flags Flags which are passed to the underlying socket.
 * 
 * @returns The number of bytes sent if successful, an MQTTErrors otherwise.
 */
ssize_t mqtt_pal_sendall(mqtt_pal_socket_handle fd, const void* buf,
			 size_t len, int flags);

/**
 * @brief Non-blocking receive all the byte available.
 * @ingroup pal
 * 
 * @param[in] fd The file-descriptor (or handle) of the socket.
 * @param[in] buf A pointer to the receive buffer.
 * @param[in] bufsz The max number of bytes that can be put into buf.
 * @param[in] flags Flags which are passed to the underlying socket.
 * 
 * @returns The number of bytes received if successful, an MQTTErrors otherwise.
 */
ssize_t mqtt_pal_recvall(mqtt_pal_socket_handle fd, void* buf, size_t bufsz,
			 int flags);

#endif
