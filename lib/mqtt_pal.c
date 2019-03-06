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

#include <bathos/bathos.h>
#include <bathos/pipe.h>
#include <bathos/mqtt.h>

/*
 * Send all @buf or return error
 */
ssize_t mqtt_pal_sendall(mqtt_pal_socket_handle fd, const void* buf,
			 size_t len, int flags)
{
	size_t sent;
	int stat;

	for (sent = 0; sent < len; sent += stat) {
		stat = pipe_write(fd, buf, len);
		if (stat < 0)
			return MQTT_ERROR_SOCKET_ERROR;
	}
	return sent;
}

/*
 * Receive everything available
 */
ssize_t mqtt_pal_recvall(mqtt_pal_socket_handle fd, void* buf, size_t bufsz,
			 int flags)
{
	struct bathos_pipe *pipe = fd;
	int ret;

	ret = pipe_read(fd, buf, bufsz);
	if (ret == -EAGAIN)
		/* Nothing available */
		ret = 0;
	return ret;
}
