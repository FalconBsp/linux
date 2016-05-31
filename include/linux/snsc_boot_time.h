/*
 *  Boot time analysis
 *
 *  Copyright 2001-2009 Sony Corporation
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  version 2 of the  License.
 *
 *  THIS  SOFTWARE  IS PROVIDED   ``AS  IS'' AND   ANY  EXPRESS OR IMPLIED
 *  WARRANTIES,   INCLUDING, BUT NOT  LIMITED  TO, THE IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN
 *  NO  EVENT  SHALL   THE AUTHOR  BE    LIABLE FOR ANY   DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 *  NOT LIMITED   TO, PROCUREMENT OF  SUBSTITUTE GOODS  OR SERVICES; LOSS OF
 *  USE, DATA,  OR PROFITS; OR  BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 *  ANY THEORY OF LIABILITY, WHETHER IN  CONTRACT, STRICT LIABILITY, OR TORT
 *  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 *  THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *  You should have received a copy of the  GNU General Public License along
 *  with this program; if not, write  to the Free Software Foundation, Inc.,
 *  51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA.
 */

#ifndef _SNSC_BOOT_TIME_H_
#define _SNSC_BOOT_TIME_H_

#ifdef CONFIG_SNSC_BOOT_TIME

void boot_time_init(void);
void boot_time_add(char *comment);
unsigned long long boot_time_cpu_clock(int cpu);

#define BOOT_TIME_ADD()         boot_time_add((char *)__FUNCTION__)
#define BOOT_TIME_ADD1(s)       boot_time_add(s)

#else /* ! CONFIG_SNSC_BOOT_TIME */
/*
 * if CONFIG_SNSC_BOOT_TIME is not defined, BOOT_TIME_ADD* do nothing
 */
#define BOOT_TIME_ADD()         do { } while(0)
#define BOOT_TIME_ADD1(s)       do { } while(0)

#endif /* CONFIG_SNSC_BOOT_TIME */

#endif /* _SNSC_BOOT_TIME_H_ */
