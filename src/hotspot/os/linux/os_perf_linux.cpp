/*
 * Copyright (c) 2012, 2020, Oracle and/or its affiliates. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * version 2 for more details (a copy is included in the LICENSE file that
 * accompanied this code).
 *
 * You should have received a copy of the GNU General Public License version
 * 2 along with this work; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Please contact Oracle, 500 Oracle Parkway, Redwood Shores, CA 94065 USA
 * or visit www.oracle.com if you need additional information or have any
 * questions.
 *
 */

#include "precompiled.hpp"
#include "jvm.h"
#include "memory/allocation.inline.hpp"
#include "os_linux.inline.hpp"
#include "runtime/os.hpp"
#include "runtime/os_perf.hpp"
#include "utilities/globalDefinitions.hpp"

#include CPU_HEADER(vm_version_ext)

#include <stdio.h>
#include <stdarg.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <pthread.h>
#include <limits.h>
#ifndef __ANDROID__
#include <ifaddrs.h>
#else
#include <sys/cdefs.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <linux/if_packet.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <net/if.h>
#include <stdint.h>
#endif
#include <fcntl.h>

#ifdef __ANDROID__
/*
 * Copyright (C) 2015 The Android Open Source Project
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 * OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

__BEGIN_DECLS

/**
 * Returned by getifaddrs() and freed by freeifaddrs().
 */
struct ifaddrs {
  /** Pointer to the next element in the linked list. */
  struct ifaddrs* ifa_next;

  /** Interface name. */
  char* ifa_name;

  /** Interface flags (like `SIOCGIFFLAGS`). */
  unsigned int ifa_flags;

  /** Interface address. */
  struct sockaddr* ifa_addr;

  /** Interface netmask. */
  struct sockaddr* ifa_netmask;

  union {
    /** Interface broadcast address (if IFF_BROADCAST is set). */
    struct sockaddr* ifu_broadaddr;

    /** Interface destination address (if IFF_POINTOPOINT is set). */
    struct sockaddr* ifu_dstaddr;
  } ifa_ifu;

  /** Unused. */
  void* ifa_data;
};

/** Synonym for `ifa_ifu.ifu_broadaddr` in `struct ifaddrs`. */
#define ifa_broadaddr ifa_ifu.ifu_broadaddr

/** Synonym for `ifa_ifu.ifu_dstaddr` in `struct ifaddrs`. */
#define ifa_dstaddr ifa_ifu.ifu_dstaddr

/**
 * [getifaddrs(3)](http://man7.org/linux/man-pages/man3/getifaddrs.3.html) creates a linked list
 * of `struct ifaddrs`. The list must be freed by freeifaddrs().
 *
 * Returns 0 and stores the list in `*__list_ptr` on success,
 * and returns -1 and sets `errno` on failure.
 *
 * Available since API level 24.
 */
int getifaddrs(struct ifaddrs** __list_ptr); // __INTRODUCED_IN(24);

/**
 * [freeifaddrs(3)](http://man7.org/linux/man-pages/man3/freeifaddrs.3.html) frees a linked list
 * of `struct ifaddrs` returned by getifaddrs().
 *
 * Available since API level 24.
 */
void freeifaddrs(struct ifaddrs* __ptr); // __INTRODUCED_IN(24);

__END_DECLS

#define nullptr 0

// The public ifaddrs struct is full of pointers. Rather than track several
// different allocations, we use a maximally-sized structure with the public
// part at offset 0, and pointers into its hidden tail.
struct ifaddrs_storage {
  // Must come first, so that `ifaddrs_storage` is-a `ifaddrs`.
  ifaddrs ifa;
  // The interface index, so we can match RTM_NEWADDR messages with
  // earlier RTM_NEWLINK messages (to copy the interface flags).
  int interface_index;
  // Storage for the pointers in `ifa`.
  sockaddr_storage addr;
  sockaddr_storage netmask;
  sockaddr_storage ifa_ifu;
  char name[IFNAMSIZ + 1];
  // Netlink gives us the address family in the header, and the
  // sockaddr_in or sockaddr_in6 bytes as the payload. We need to
  // stitch the two bits together into the sockaddr that's part of
  // our portable interface.
  void SetAddress(int family, const void* data, size_t byteCount) {
      addr.ss_family = family;
      memcpy(SockaddrBytes(family, &addr), data, byteCount);
      ifa.ifa_addr = reinterpret_cast<sockaddr*>(&addr);
  }
  void SetBroadcastAddress(int family, const void* data, size_t byteCount) {
      ifa_ifu.ss_family = family;
      memcpy(SockaddrBytes(family, &ifa_ifu), data, byteCount);
      ifa.ifa_dstaddr = reinterpret_cast<sockaddr*>(&ifa_ifu);
  }
  // Netlink gives us the prefix length as a bit count. We need to turn
  // that into a BSD-compatible netmask represented by a sockaddr*.
  void SetNetmask(int family, size_t prefix_length) {
      // ...and work out the netmask from the prefix length.
      netmask.ss_family = family;
      uint8_t* dst = SockaddrBytes(family, &netmask);
      memset(dst, 0xff, prefix_length / 8);
      if ((prefix_length % 8) != 0) {
        dst[prefix_length/8] = (0xff << (8 - (prefix_length % 8)));
      }
      ifa.ifa_netmask = reinterpret_cast<sockaddr*>(&netmask);
  }
  void SetPacketAttributes(int ifindex, unsigned short hatype, unsigned char halen) {
    sockaddr_ll* sll = reinterpret_cast<sockaddr_ll*>(&addr);
    sll->sll_ifindex = ifindex;
    sll->sll_hatype = hatype;
    sll->sll_halen = halen;
  }
 private:
  // Returns a pointer to the first byte in the address data (which is
  // stored in network byte order).
  uint8_t* SockaddrBytes(int family, sockaddr_storage* ss) {
    if (family == AF_INET) {
      sockaddr_in* ss4 = reinterpret_cast<sockaddr_in*>(ss);
      return reinterpret_cast<uint8_t*>(&ss4->sin_addr);
    } else if (family == AF_INET6) {
      sockaddr_in6* ss6 = reinterpret_cast<sockaddr_in6*>(ss);
      return reinterpret_cast<uint8_t*>(&ss6->sin6_addr);
    } else if (family == AF_PACKET) {
      sockaddr_ll* sll = reinterpret_cast<sockaddr_ll*>(ss);
      return reinterpret_cast<uint8_t*>(&sll->sll_addr);
    }
    return nullptr;
  }
};
ifaddrs_storage* new_ifaddrs_storage(ifaddrs** list) {
  ifaddrs_storage *storage;
  memset(storage, 0, sizeof(*storage));
  // push_front onto `list`.
  storage->ifa.ifa_next = *list;
  *list = reinterpret_cast<ifaddrs*>(storage);
  return storage;
}
#if !defined(__clang__)
// GCC gets confused by NLMSG_DATA and doesn't realize that the old-style
// cast is from a system header and should be ignored.
#pragma GCC diagnostic ignored "-Wold-style-cast"
#endif
static void __handle_netlink_response(ifaddrs** out, nlmsghdr* hdr) {
  if (hdr->nlmsg_type == RTM_NEWLINK) {
    ifinfomsg* ifi = reinterpret_cast<ifinfomsg*>(NLMSG_DATA(hdr));
    // Create a new ifaddr entry, and set the interface index and flags.
    ifaddrs_storage* new_addr = new_ifaddrs_storage(out);
    new_addr->interface_index = ifi->ifi_index;
    new_addr->ifa.ifa_flags = ifi->ifi_flags;
    // Go through the various bits of information and find the name.
    rtattr* rta = IFLA_RTA(ifi);
    size_t rta_len = IFLA_PAYLOAD(hdr);
    while (RTA_OK(rta, rta_len)) {
      if (rta->rta_type == IFLA_ADDRESS) {
          if (RTA_PAYLOAD(rta) < sizeof(new_addr->addr)) {
            new_addr->SetAddress(AF_PACKET, RTA_DATA(rta), RTA_PAYLOAD(rta));
            new_addr->SetPacketAttributes(ifi->ifi_index, ifi->ifi_type, RTA_PAYLOAD(rta));
          }
      } else if (rta->rta_type == IFLA_BROADCAST) {
          if (RTA_PAYLOAD(rta) < sizeof(new_addr->ifa_ifu)) {
            new_addr->SetBroadcastAddress(AF_PACKET, RTA_DATA(rta), RTA_PAYLOAD(rta));
            new_addr->SetPacketAttributes(ifi->ifi_index, ifi->ifi_type, RTA_PAYLOAD(rta));
          }
      } else if (rta->rta_type == IFLA_IFNAME) {
          if (RTA_PAYLOAD(rta) < sizeof(new_addr->name)) {
            memcpy(new_addr->name, RTA_DATA(rta), RTA_PAYLOAD(rta));
            new_addr->ifa.ifa_name = new_addr->name;
          }
      }
      rta = RTA_NEXT(rta, rta_len);
    }
  } else if (hdr->nlmsg_type == RTM_NEWADDR) {
    ifaddrmsg* msg = reinterpret_cast<ifaddrmsg*>(NLMSG_DATA(hdr));
    // We should already know about this from an RTM_NEWLINK message.
    const ifaddrs_storage* addr = reinterpret_cast<const ifaddrs_storage*>(*out);
    while (addr != nullptr && addr->interface_index != static_cast<int>(msg->ifa_index)) {
      addr = reinterpret_cast<const ifaddrs_storage*>(addr->ifa.ifa_next);
    }
    // If this is an unknown interface, ignore whatever we're being told about it.
    if (addr == nullptr) return;
    // Create a new ifaddr entry and copy what we already know.
    ifaddrs_storage* new_addr = new_ifaddrs_storage(out);
    // We can just copy the name rather than look for IFA_LABEL.
    strcpy(new_addr->name, addr->name);
    new_addr->ifa.ifa_name = new_addr->name;
    new_addr->ifa.ifa_flags = addr->ifa.ifa_flags;
    new_addr->interface_index = addr->interface_index;
    // Go through the various bits of information and find the address
    // and any broadcast/destination address.
    rtattr* rta = IFA_RTA(msg);
    size_t rta_len = IFA_PAYLOAD(hdr);
    while (RTA_OK(rta, rta_len)) {
      if (rta->rta_type == IFA_ADDRESS) {
        if (msg->ifa_family == AF_INET || msg->ifa_family == AF_INET6) {
          new_addr->SetAddress(msg->ifa_family, RTA_DATA(rta), RTA_PAYLOAD(rta));
          new_addr->SetNetmask(msg->ifa_family, msg->ifa_prefixlen);
        }
      } else if (rta->rta_type == IFA_BROADCAST) {
        if (msg->ifa_family == AF_INET || msg->ifa_family == AF_INET6) {
          new_addr->SetBroadcastAddress(msg->ifa_family, RTA_DATA(rta), RTA_PAYLOAD(rta));
        }
      }
      rta = RTA_NEXT(rta, rta_len);
    }
  }
}
static bool __send_netlink_request(int fd, int type) {
  struct NetlinkMessage {
    nlmsghdr hdr;
    rtgenmsg msg;
  } request;
  memset(&request, 0, sizeof(request));
  request.hdr.nlmsg_flags = NLM_F_DUMP | NLM_F_REQUEST;
  request.hdr.nlmsg_type = type;
  request.hdr.nlmsg_len = sizeof(request);
  request.msg.rtgen_family = AF_UNSPEC; // All families.
  return (TEMP_FAILURE_RETRY(send(fd, &request, sizeof(request), 0)) == sizeof(request));
}
static bool __read_netlink_responses(int fd, ifaddrs** out, char* buf, size_t buf_len) {
  ssize_t bytes_read;
  // Read through all the responses, handing interesting ones to __handle_netlink_response.
  while ((bytes_read = TEMP_FAILURE_RETRY(recv(fd, buf, buf_len, 0))) > 0) {
    nlmsghdr* hdr = reinterpret_cast<nlmsghdr*>(buf);
    for (; NLMSG_OK(hdr, static_cast<size_t>(bytes_read)); hdr = NLMSG_NEXT(hdr, bytes_read)) {
      if (hdr->nlmsg_type == NLMSG_DONE) return true;
      if (hdr->nlmsg_type == NLMSG_ERROR) return false;
      __handle_netlink_response(out, hdr);
    }
  }
  // We only get here if recv fails before we see a NLMSG_DONE.
  return false;
}
int getifaddrs(ifaddrs** out) {
  // Make cleanup easy.
  *out = nullptr;
  // The kernel keeps packets under 8KiB (NLMSG_GOODSIZE),
  // but that's a bit too large to go on the stack.
  size_t buf_len = 8192;
  char* buf = NEW_C_HEAP_ARRAY(char, buf_len, mtInternal);
  if (buf == nullptr) return -1;
  // Open the netlink socket and ask for all the links and addresses.
  int fd = socket(PF_NETLINK, SOCK_RAW | SOCK_CLOEXEC, NETLINK_ROUTE);
  bool okay = fd != -1 &&
      __send_netlink_request(fd, RTM_GETLINK) && __read_netlink_responses(fd, out, buf, buf_len) &&
      __send_netlink_request(fd, RTM_GETADDR) && __read_netlink_responses(fd, out, buf, buf_len);
  if (!okay) {
    freeifaddrs(*out);
    // Ensure that callers crash if they forget to check for success.
    *out = nullptr;
  }
  {
    int saved_errno = errno;
    close(fd);
    FREE_C_HEAP_ARRAY(char, buf);
    errno = saved_errno;
  }
  return okay ? 0 : -1;
}
void freeifaddrs(ifaddrs* list) {
  while (list != nullptr) {
    ifaddrs* current = list;
    list = list->ifa_next;
    free(current);
  }
}
#endif

/**
   /proc/[number]/stat
              Status information about the process.  This is used by ps(1).  It is defined in /usr/src/linux/fs/proc/array.c.

              The fields, in order, with their proper scanf(3) format specifiers, are:

              1. pid %d The process id.

              2. comm %s
                     The filename of the executable, in parentheses.  This is visible whether or not the executable is swapped out.

              3. state %c
                     One  character  from  the  string "RSDZTW" where R is running, S is sleeping in an interruptible wait, D is waiting in uninterruptible disk
                     sleep, Z is zombie, T is traced or stopped (on a signal), and W is paging.

              4. ppid %d
                     The PID of the parent.

              5. pgrp %d
                     The process group ID of the process.

              6. session %d
                     The session ID of the process.

              7. tty_nr %d
                     The tty the process uses.

              8. tpgid %d
                     The process group ID of the process which currently owns the tty that the process is connected to.

              9. flags %lu
                     The flags of the process.  The math bit is decimal 4, and the traced bit is decimal 10.

              10. minflt %lu
                     The number of minor faults the process has made which have not required loading a memory page from disk.

              11. cminflt %lu
                     The number of minor faults that the process's waited-for children have made.

              12. majflt %lu
                     The number of major faults the process has made which have required loading a memory page from disk.

              13. cmajflt %lu
                     The number of major faults that the process's waited-for children have made.

              14. utime %lu
                     The number of jiffies that this process has been scheduled in user mode.

              15. stime %lu
                     The number of jiffies that this process has been scheduled in kernel mode.

              16. cutime %ld
                     The number of jiffies that this process's waited-for children have been scheduled in user mode. (See also times(2).)

              17. cstime %ld
                     The number of jiffies that this process' waited-for children have been scheduled in kernel mode.

              18. priority %ld
                     The standard nice value, plus fifteen.  The value is never negative in the kernel.

              19. nice %ld
                     The nice value ranges from 19 (nicest) to -19 (not nice to others).

              20. 0 %ld  This value is hard coded to 0 as a placeholder for a removed field.

              21. itrealvalue %ld
                     The time in jiffies before the next SIGALRM is sent to the process due to an interval timer.

              22. starttime %lu
                     The time in jiffies the process started after system boot.

              23. vsize %lu
                     Virtual memory size in bytes.

              24. rss %ld
                     Resident Set Size: number of pages the process has in real memory, minus 3 for administrative purposes. This is just the pages which  count
                     towards text, data, or stack space.  This does not include pages which have not been demand-loaded in, or which are swapped out.

              25. rlim %lu
                     Current limit in bytes on the rss of the process (usually 4294967295 on i386).

              26. startcode %lu
                     The address above which program text can run.

              27. endcode %lu
                     The address below which program text can run.

              28. startstack %lu
                     The address of the start of the stack.

              29. kstkesp %lu
                     The current value of esp (stack pointer), as found in the kernel stack page for the process.

              30. kstkeip %lu
                     The current EIP (instruction pointer).

              31. signal %lu
                     The bitmap of pending signals (usually 0).

              32. blocked %lu
                     The bitmap of blocked signals (usually 0, 2 for shells).

              33. sigignore %lu
                     The bitmap of ignored signals.

              34. sigcatch %lu
                     The bitmap of catched signals.

              35. wchan %lu
                     This  is the "channel" in which the process is waiting.  It is the address of a system call, and can be looked up in a namelist if you need
                     a textual name.  (If you have an up-to-date /etc/psdatabase, then try ps -l to see the WCHAN field in action.)

              36. nswap %lu
                     Number of pages swapped - not maintained.

              37. cnswap %lu
                     Cumulative nswap for child processes.

              38. exit_signal %d
                     Signal to be sent to parent when we die.

              39. processor %d
                     CPU number last executed on.



 ///// SSCANF FORMAT STRING. Copy and use.

field:        1  2  3  4  5  6  7  8  9   10  11  12  13  14  15  16  17  18  19  20  21  22  23  24  25  26  27  28  29  30  31  32  33  34  35  36  37  38 39
format:       %d %s %c %d %d %d %d %d %lu %lu %lu %lu %lu %lu %lu %ld %ld %ld %ld %ld %ld %lu %lu %ld %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %d %d


*/

/**
 * For platforms that have them, when declaring
 * a printf-style function,
 *   formatSpec is the parameter number (starting at 1)
 *       that is the format argument ("%d pid %s")
 *   params is the parameter number where the actual args to
 *       the format starts. If the args are in a va_list, this
 *       should be 0.
 */
#ifndef PRINTF_ARGS
#  define PRINTF_ARGS(formatSpec,  params) ATTRIBUTE_PRINTF(formatSpec, params)
#endif

#ifndef SCANF_ARGS
#  define SCANF_ARGS(formatSpec,   params) ATTRIBUTE_SCANF(formatSpec, params)
#endif

#ifndef _PRINTFMT_
#  define _PRINTFMT_
#endif

#ifndef _SCANFMT_
#  define _SCANFMT_
#endif

typedef enum {
  CPU_LOAD_VM_ONLY,
  CPU_LOAD_GLOBAL,
} CpuLoadTarget;

enum {
  UNDETECTED,
  UNDETECTABLE,
  LINUX26_NPTL,
  BAREMETAL
};

struct CPUPerfCounters {
  int   nProcs;
  os::Linux::CPUPerfTicks jvmTicks;
  os::Linux::CPUPerfTicks* cpus;
};

static double get_cpu_load(int which_logical_cpu, CPUPerfCounters* counters, double* pkernelLoad, CpuLoadTarget target);

/** reads /proc/<pid>/stat data, with some checks and some skips.
 *  Ensure that 'fmt' does _NOT_ contain the first two "%d %s"
 */
static int SCANF_ARGS(2, 0) vread_statdata(const char* procfile, _SCANFMT_ const char* fmt, va_list args) {
  FILE*f;
  int n;
  char buf[2048];

  if ((f = fopen(procfile, "r")) == NULL) {
    return -1;
  }

  if ((n = fread(buf, 1, sizeof(buf), f)) != -1) {
    char *tmp;

    buf[n-1] = '\0';
    /** skip through pid and exec name. */
    if ((tmp = strrchr(buf, ')')) != NULL) {
      // skip the ')' and the following space
      // but check that buffer is long enough
      tmp += 2;
      if (tmp < buf + n) {
        n = vsscanf(tmp, fmt, args);
      }
    }
  }

  fclose(f);

  return n;
}

static int SCANF_ARGS(2, 3) read_statdata(const char* procfile, _SCANFMT_ const char* fmt, ...) {
  int   n;
  va_list args;

  va_start(args, fmt);
  n = vread_statdata(procfile, fmt, args);
  va_end(args);
  return n;
}

static FILE* open_statfile(void) {
  FILE *f;

  if ((f = fopen("/proc/stat", "r")) == NULL) {
    static int haveWarned = 0;
    if (!haveWarned) {
      haveWarned = 1;
    }
  }
  return f;
}

static int get_systemtype(void) {
  static int procEntriesType = UNDETECTED;
  DIR *taskDir;

  if (procEntriesType != UNDETECTED) {
    return procEntriesType;
  }

  // Check whether we have a task subdirectory
  if ((taskDir = opendir("/proc/self/task")) == NULL) {
    procEntriesType = UNDETECTABLE;
  } else {
    // The task subdirectory exists; we're on a Linux >= 2.6 system
    closedir(taskDir);
    procEntriesType = LINUX26_NPTL;
  }

  return procEntriesType;
}

/** read user and system ticks from a named procfile, assumed to be in 'stat' format then. */
static int read_ticks(const char* procfile, uint64_t* userTicks, uint64_t* systemTicks) {
  return read_statdata(procfile, "%*c %*d %*d %*d %*d %*d %*u %*u %*u %*u %*u " UINT64_FORMAT " " UINT64_FORMAT,
    userTicks, systemTicks);
}

/**
 * Return the number of ticks spent in any of the processes belonging
 * to the JVM on any CPU.
 */
static OSReturn get_jvm_ticks(os::Linux::CPUPerfTicks* pticks) {
  uint64_t userTicks;
  uint64_t systemTicks;

  if (get_systemtype() != LINUX26_NPTL) {
    return OS_ERR;
  }

  if (read_ticks("/proc/self/stat", &userTicks, &systemTicks) != 2) {
    return OS_ERR;
  }

  // get the total
  if (! os::Linux::get_tick_information(pticks, -1)) {
    return OS_ERR;
  }

  pticks->used       = userTicks;
  pticks->usedKernel = systemTicks;

  return OS_OK;
}

/**
 * Return the load of the CPU as a double. 1.0 means the CPU process uses all
 * available time for user or system processes, 0.0 means the CPU uses all time
 * being idle.
 *
 * Returns a negative value if there is a problem in determining the CPU load.
 */
static double get_cpu_load(int which_logical_cpu, CPUPerfCounters* counters, double* pkernelLoad, CpuLoadTarget target) {
  uint64_t udiff, kdiff, tdiff;
  os::Linux::CPUPerfTicks* pticks;
  os::Linux::CPUPerfTicks  tmp;
  double user_load;

  *pkernelLoad = 0.0;

  if (target == CPU_LOAD_VM_ONLY) {
    pticks = &counters->jvmTicks;
  } else if (-1 == which_logical_cpu) {
    pticks = &counters->cpus[counters->nProcs];
  } else {
    pticks = &counters->cpus[which_logical_cpu];
  }

  tmp = *pticks;

  if (target == CPU_LOAD_VM_ONLY) {
    if (get_jvm_ticks(pticks) != OS_OK) {
      return -1.0;
    }
  } else if (! os::Linux::get_tick_information(pticks, which_logical_cpu)) {
    return -1.0;
  }

  // seems like we sometimes end up with less kernel ticks when
  // reading /proc/self/stat a second time, timing issue between cpus?
  if (pticks->usedKernel < tmp.usedKernel) {
    kdiff = 0;
  } else {
    kdiff = pticks->usedKernel - tmp.usedKernel;
  }
  tdiff = pticks->total - tmp.total;
  udiff = pticks->used - tmp.used;

  if (tdiff == 0) {
    return 0.0;
  } else if (tdiff < (udiff + kdiff)) {
    tdiff = udiff + kdiff;
  }
  *pkernelLoad = (kdiff / (double)tdiff);
  // BUG9044876, normalize return values to sane values
  *pkernelLoad = MAX2<double>(*pkernelLoad, 0.0);
  *pkernelLoad = MIN2<double>(*pkernelLoad, 1.0);

  user_load = (udiff / (double)tdiff);
  user_load = MAX2<double>(user_load, 0.0);
  user_load = MIN2<double>(user_load, 1.0);

  return user_load;
}

static int SCANF_ARGS(1, 2) parse_stat(_SCANFMT_ const char* fmt, ...) {
  FILE *f;
  va_list args;

  va_start(args, fmt);

  if ((f = open_statfile()) == NULL) {
    va_end(args);
    return OS_ERR;
  }
  for (;;) {
    char line[80];
    if (fgets(line, sizeof(line), f) != NULL) {
      if (vsscanf(line, fmt, args) == 1) {
        fclose(f);
        va_end(args);
        return OS_OK;
      }
    } else {
        fclose(f);
        va_end(args);
        return OS_ERR;
    }
  }
}

static int get_noof_context_switches(uint64_t* switches) {
  return parse_stat("ctxt " UINT64_FORMAT "\n", switches);
}

/** returns boot time in _seconds_ since epoch */
static int get_boot_time(uint64_t* time) {
  return parse_stat("btime " UINT64_FORMAT "\n", time);
}

static int perf_context_switch_rate(double* rate) {
  static pthread_mutex_t contextSwitchLock = PTHREAD_MUTEX_INITIALIZER;
  static uint64_t      bootTime;
  static uint64_t      lastTimeNanos;
  static uint64_t      lastSwitches;
  static double        lastRate;

  uint64_t bt = 0;
  int res = 0;

  // First time through bootTime will be zero.
  if (bootTime == 0) {
    uint64_t tmp;
    if (get_boot_time(&tmp) < 0) {
      return OS_ERR;
    }
    bt = tmp * 1000;
  }

  res = OS_OK;

  pthread_mutex_lock(&contextSwitchLock);
  {

    uint64_t sw;
    s8 t, d;

    if (bootTime == 0) {
      // First interval is measured from boot time which is
      // seconds since the epoch. Thereafter we measure the
      // elapsed time using javaTimeNanos as it is monotonic-
      // non-decreasing.
      lastTimeNanos = os::javaTimeNanos();
      t = os::javaTimeMillis();
      d = t - bt;
      // keep bootTime zero for now to use as a first-time-through flag
    } else {
      t = os::javaTimeNanos();
      d = nanos_to_millis(t - lastTimeNanos);
    }

    if (d == 0) {
      *rate = lastRate;
    } else if (get_noof_context_switches(&sw) == 0) {
      *rate      = ( (double)(sw - lastSwitches) / d ) * 1000;
      lastRate     = *rate;
      lastSwitches = sw;
      if (bootTime != 0) {
        lastTimeNanos = t;
      }
    } else {
      *rate = 0;
      res   = OS_ERR;
    }
    if (*rate <= 0) {
      *rate = 0;
      lastRate = 0;
    }

    if (bootTime == 0) {
      bootTime = bt;
    }
  }
  pthread_mutex_unlock(&contextSwitchLock);

  return res;
}

class CPUPerformanceInterface::CPUPerformance : public CHeapObj<mtInternal> {
  friend class CPUPerformanceInterface;
 private:
  CPUPerfCounters _counters;

  int cpu_load(int which_logical_cpu, double* cpu_load);
  int context_switch_rate(double* rate);
  int cpu_load_total_process(double* cpu_load);
  int cpu_loads_process(double* pjvmUserLoad, double* pjvmKernelLoad, double* psystemTotalLoad);

 public:
  CPUPerformance();
  bool initialize();
  ~CPUPerformance();
};

CPUPerformanceInterface::CPUPerformance::CPUPerformance() {
  _counters.nProcs = os::active_processor_count();
  _counters.cpus = NULL;
}

bool CPUPerformanceInterface::CPUPerformance::initialize() {
  size_t array_entry_count = _counters.nProcs + 1;
  _counters.cpus = NEW_C_HEAP_ARRAY(os::Linux::CPUPerfTicks, array_entry_count, mtInternal);
  memset(_counters.cpus, 0, array_entry_count * sizeof(*_counters.cpus));

  // For the CPU load total
  os::Linux::get_tick_information(&_counters.cpus[_counters.nProcs], -1);

  // For each CPU
  for (int i = 0; i < _counters.nProcs; i++) {
    os::Linux::get_tick_information(&_counters.cpus[i], i);
  }
  // For JVM load
  get_jvm_ticks(&_counters.jvmTicks);

  // initialize context switch system
  // the double is only for init
  double init_ctx_switch_rate;
  perf_context_switch_rate(&init_ctx_switch_rate);

  return true;
}

CPUPerformanceInterface::CPUPerformance::~CPUPerformance() {
  if (_counters.cpus != NULL) {
    FREE_C_HEAP_ARRAY(char, _counters.cpus);
  }
}

int CPUPerformanceInterface::CPUPerformance::cpu_load(int which_logical_cpu, double* cpu_load) {
  double u, s;
  u = get_cpu_load(which_logical_cpu, &_counters, &s, CPU_LOAD_GLOBAL);
  if (u < 0) {
    *cpu_load = 0.0;
    return OS_ERR;
  }
  // Cap total systemload to 1.0
  *cpu_load = MIN2<double>((u + s), 1.0);
  return OS_OK;
}

int CPUPerformanceInterface::CPUPerformance::cpu_load_total_process(double* cpu_load) {
  double u, s;
  u = get_cpu_load(-1, &_counters, &s, CPU_LOAD_VM_ONLY);
  if (u < 0) {
    *cpu_load = 0.0;
    return OS_ERR;
  }
  *cpu_load = u + s;
  return OS_OK;
}

int CPUPerformanceInterface::CPUPerformance::cpu_loads_process(double* pjvmUserLoad, double* pjvmKernelLoad, double* psystemTotalLoad) {
  double u, s, t;

  assert(pjvmUserLoad != NULL, "pjvmUserLoad not inited");
  assert(pjvmKernelLoad != NULL, "pjvmKernelLoad not inited");
  assert(psystemTotalLoad != NULL, "psystemTotalLoad not inited");

  u = get_cpu_load(-1, &_counters, &s, CPU_LOAD_VM_ONLY);
  if (u < 0) {
    *pjvmUserLoad = 0.0;
    *pjvmKernelLoad = 0.0;
    *psystemTotalLoad = 0.0;
    return OS_ERR;
  }

  cpu_load(-1, &t);
  // clamp at user+system and 1.0
  if (u + s > t) {
    t = MIN2<double>(u + s, 1.0);
  }

  *pjvmUserLoad = u;
  *pjvmKernelLoad = s;
  *psystemTotalLoad = t;

  return OS_OK;
}

int CPUPerformanceInterface::CPUPerformance::context_switch_rate(double* rate) {
  return perf_context_switch_rate(rate);
}

CPUPerformanceInterface::CPUPerformanceInterface() {
  _impl = NULL;
}

bool CPUPerformanceInterface::initialize() {
  _impl = new CPUPerformanceInterface::CPUPerformance();
  return _impl->initialize();
}

CPUPerformanceInterface::~CPUPerformanceInterface() {
  if (_impl != NULL) {
    delete _impl;
  }
}

int CPUPerformanceInterface::cpu_load(int which_logical_cpu, double* cpu_load) const {
  return _impl->cpu_load(which_logical_cpu, cpu_load);
}

int CPUPerformanceInterface::cpu_load_total_process(double* cpu_load) const {
  return _impl->cpu_load_total_process(cpu_load);
}

int CPUPerformanceInterface::cpu_loads_process(double* pjvmUserLoad, double* pjvmKernelLoad, double* psystemTotalLoad) const {
  return _impl->cpu_loads_process(pjvmUserLoad, pjvmKernelLoad, psystemTotalLoad);
}

int CPUPerformanceInterface::context_switch_rate(double* rate) const {
  return _impl->context_switch_rate(rate);
}

class SystemProcessInterface::SystemProcesses : public CHeapObj<mtInternal> {
  friend class SystemProcessInterface;
 private:
  class ProcessIterator : public CHeapObj<mtInternal> {
    friend class SystemProcessInterface::SystemProcesses;
   private:
    DIR*           _dir;
    struct dirent* _entry;
    bool           _valid;
    char           _exeName[PATH_MAX];
    char           _exePath[PATH_MAX];

    ProcessIterator();
    ~ProcessIterator();
    bool initialize();

    bool is_valid() const { return _valid; }
    bool is_valid_entry(struct dirent* entry) const;
    bool is_dir(const char* name) const;
    int  fsize(const char* name, uint64_t& size) const;

    char* allocate_string(const char* str) const;
    void  get_exe_name();
    char* get_exe_path();
    char* get_cmdline();

    int current(SystemProcess* process_info);
    int next_process();
  };

  ProcessIterator* _iterator;
  SystemProcesses();
  bool initialize();
  ~SystemProcesses();

  //information about system processes
  int system_processes(SystemProcess** system_processes, int* no_of_sys_processes) const;
};

bool SystemProcessInterface::SystemProcesses::ProcessIterator::is_dir(const char* name) const {
  struct stat mystat;
  int ret_val = 0;

  ret_val = stat(name, &mystat);
  if (ret_val < 0) {
    return false;
  }
  ret_val = S_ISDIR(mystat.st_mode);
  return ret_val > 0;
}

int SystemProcessInterface::SystemProcesses::ProcessIterator::fsize(const char* name, uint64_t& size) const {
  assert(name != NULL, "name pointer is NULL!");
  size = 0;
  struct stat fbuf;

  if (stat(name, &fbuf) < 0) {
    return OS_ERR;
  }
  size = fbuf.st_size;
  return OS_OK;
}

// if it has a numeric name, is a directory and has a 'stat' file in it
bool SystemProcessInterface::SystemProcesses::ProcessIterator::is_valid_entry(struct dirent* entry) const {
  char buffer[PATH_MAX];
  uint64_t size = 0;

  if (atoi(entry->d_name) != 0) {
    jio_snprintf(buffer, PATH_MAX, "/proc/%s", entry->d_name);
    buffer[PATH_MAX - 1] = '\0';

    if (is_dir(buffer)) {
      jio_snprintf(buffer, PATH_MAX, "/proc/%s/stat", entry->d_name);
      buffer[PATH_MAX - 1] = '\0';
      if (fsize(buffer, size) != OS_ERR) {
        return true;
      }
    }
  }
  return false;
}

// get exe-name from /proc/<pid>/stat
void SystemProcessInterface::SystemProcesses::ProcessIterator::get_exe_name() {
  FILE* fp;
  char  buffer[PATH_MAX];

  jio_snprintf(buffer, PATH_MAX, "/proc/%s/stat", _entry->d_name);
  buffer[PATH_MAX - 1] = '\0';
  if ((fp = fopen(buffer, "r")) != NULL) {
    if (fgets(buffer, PATH_MAX, fp) != NULL) {
      char* start, *end;
      // exe-name is between the first pair of ( and )
      start = strchr(buffer, '(');
      if (start != NULL && start[1] != '\0') {
        start++;
        end = strrchr(start, ')');
        if (end != NULL) {
          size_t len;
          len = MIN2<size_t>(end - start, sizeof(_exeName) - 1);
          memcpy(_exeName, start, len);
          _exeName[len] = '\0';
        }
      }
    }
    fclose(fp);
  }
}

// get command line from /proc/<pid>/cmdline
char* SystemProcessInterface::SystemProcesses::ProcessIterator::get_cmdline() {
  FILE* fp;
  char  buffer[PATH_MAX];
  char* cmdline = NULL;

  jio_snprintf(buffer, PATH_MAX, "/proc/%s/cmdline", _entry->d_name);
  buffer[PATH_MAX - 1] = '\0';
  if ((fp = fopen(buffer, "r")) != NULL) {
    size_t size = 0;
    char   dummy;

    // find out how long the file is (stat always returns 0)
    while (fread(&dummy, 1, 1, fp) == 1) {
      size++;
    }
    if (size > 0) {
      cmdline = NEW_C_HEAP_ARRAY(char, size + 1, mtInternal);
      cmdline[0] = '\0';
      if (fseek(fp, 0, SEEK_SET) == 0) {
        if (fread(cmdline, 1, size, fp) == size) {
          // the file has the arguments separated by '\0',
          // so we translate '\0' to ' '
          for (size_t i = 0; i < size; i++) {
            if (cmdline[i] == '\0') {
              cmdline[i] = ' ';
            }
          }
          cmdline[size] = '\0';
        }
      }
    }
    fclose(fp);
  }
  return cmdline;
}

// get full path to exe from /proc/<pid>/exe symlink
char* SystemProcessInterface::SystemProcesses::ProcessIterator::get_exe_path() {
  char buffer[PATH_MAX];

  jio_snprintf(buffer, PATH_MAX, "/proc/%s/exe", _entry->d_name);
  buffer[PATH_MAX - 1] = '\0';
  return realpath(buffer, _exePath);
}

char* SystemProcessInterface::SystemProcesses::ProcessIterator::allocate_string(const char* str) const {
  if (str != NULL) {
    return os::strdup_check_oom(str, mtInternal);
  }
  return NULL;
}

int SystemProcessInterface::SystemProcesses::ProcessIterator::current(SystemProcess* process_info) {
  if (!is_valid()) {
    return OS_ERR;
  }

  process_info->set_pid(atoi(_entry->d_name));

  get_exe_name();
  process_info->set_name(allocate_string(_exeName));

  if (get_exe_path() != NULL) {
     process_info->set_path(allocate_string(_exePath));
  }

  char* cmdline = NULL;
  cmdline = get_cmdline();
  if (cmdline != NULL) {
    process_info->set_command_line(allocate_string(cmdline));
    FREE_C_HEAP_ARRAY(char, cmdline);
  }

  return OS_OK;
}

int SystemProcessInterface::SystemProcesses::ProcessIterator::next_process() {
  if (!is_valid()) {
    return OS_ERR;
  }

  do {
    _entry = os::readdir(_dir);
    if (_entry == NULL) {
      // Error or reached end.  Could use errno to distinguish those cases.
      _valid = false;
      return OS_ERR;
    }
  } while(!is_valid_entry(_entry));

  _valid = true;
  return OS_OK;
}

SystemProcessInterface::SystemProcesses::ProcessIterator::ProcessIterator() {
  _dir = NULL;
  _entry = NULL;
  _valid = false;
}

bool SystemProcessInterface::SystemProcesses::ProcessIterator::initialize() {
  _dir = os::opendir("/proc");
  _entry = NULL;
  _valid = true;
  next_process();

  return true;
}

SystemProcessInterface::SystemProcesses::ProcessIterator::~ProcessIterator() {
  if (_dir != NULL) {
    os::closedir(_dir);
  }
}

SystemProcessInterface::SystemProcesses::SystemProcesses() {
  _iterator = NULL;
}

bool SystemProcessInterface::SystemProcesses::initialize() {
  _iterator = new SystemProcessInterface::SystemProcesses::ProcessIterator();
  return _iterator->initialize();
}

SystemProcessInterface::SystemProcesses::~SystemProcesses() {
  if (_iterator != NULL) {
    delete _iterator;
  }
}

int SystemProcessInterface::SystemProcesses::system_processes(SystemProcess** system_processes, int* no_of_sys_processes) const {
  assert(system_processes != NULL, "system_processes pointer is NULL!");
  assert(no_of_sys_processes != NULL, "system_processes counter pointers is NULL!");
  assert(_iterator != NULL, "iterator is NULL!");

  // initialize pointers
  *no_of_sys_processes = 0;
  *system_processes = NULL;

  while (_iterator->is_valid()) {
    SystemProcess* tmp = new SystemProcess();
    _iterator->current(tmp);

    //if already existing head
    if (*system_processes != NULL) {
      //move "first to second"
      tmp->set_next(*system_processes);
    }
    // new head
    *system_processes = tmp;
    // increment
    (*no_of_sys_processes)++;
    // step forward
    _iterator->next_process();
  }
  return OS_OK;
}

int SystemProcessInterface::system_processes(SystemProcess** system_procs, int* no_of_sys_processes) const {
  return _impl->system_processes(system_procs, no_of_sys_processes);
}

SystemProcessInterface::SystemProcessInterface() {
  _impl = NULL;
}

bool SystemProcessInterface::initialize() {
  _impl = new SystemProcessInterface::SystemProcesses();
  return _impl->initialize();
}

SystemProcessInterface::~SystemProcessInterface() {
  if (_impl != NULL) {
    delete _impl;
  }
}

CPUInformationInterface::CPUInformationInterface() {
  _cpu_info = NULL;
}

bool CPUInformationInterface::initialize() {
  _cpu_info = new CPUInformation();
  _cpu_info->set_number_of_hardware_threads(VM_Version_Ext::number_of_threads());
  _cpu_info->set_number_of_cores(VM_Version_Ext::number_of_cores());
  _cpu_info->set_number_of_sockets(VM_Version_Ext::number_of_sockets());
  _cpu_info->set_cpu_name(VM_Version_Ext::cpu_name());
  _cpu_info->set_cpu_description(VM_Version_Ext::cpu_description());
  return true;
}

CPUInformationInterface::~CPUInformationInterface() {
  if (_cpu_info != NULL) {
    if (_cpu_info->cpu_name() != NULL) {
      const char* cpu_name = _cpu_info->cpu_name();
      FREE_C_HEAP_ARRAY(char, cpu_name);
      _cpu_info->set_cpu_name(NULL);
    }
    if (_cpu_info->cpu_description() != NULL) {
       const char* cpu_desc = _cpu_info->cpu_description();
       FREE_C_HEAP_ARRAY(char, cpu_desc);
      _cpu_info->set_cpu_description(NULL);
    }
    delete _cpu_info;
  }
}

int CPUInformationInterface::cpu_information(CPUInformation& cpu_info) {
  if (_cpu_info == NULL) {
    return OS_ERR;
  }

  cpu_info = *_cpu_info; // shallow copy assignment
  return OS_OK;
}

class NetworkPerformanceInterface::NetworkPerformance : public CHeapObj<mtInternal> {
  friend class NetworkPerformanceInterface;
 private:
  NetworkPerformance();
  NONCOPYABLE(NetworkPerformance);
  bool initialize();
  ~NetworkPerformance();
  int64_t read_counter(const char* iface, const char* counter) const;
  int network_utilization(NetworkInterface** network_interfaces) const;
};

NetworkPerformanceInterface::NetworkPerformance::NetworkPerformance() {

}

bool NetworkPerformanceInterface::NetworkPerformance::initialize() {
  return true;
}

NetworkPerformanceInterface::NetworkPerformance::~NetworkPerformance() {
}

int64_t NetworkPerformanceInterface::NetworkPerformance::read_counter(const char* iface, const char* counter) const {
  char buf[128];

  snprintf(buf, sizeof(buf), "/sys/class/net/%s/statistics/%s", iface, counter);

  int fd = os::open(buf, O_RDONLY, 0);
  if (fd == -1) {
    return -1;
  }

  ssize_t num_bytes = read(fd, buf, sizeof(buf));
  close(fd);
  if ((num_bytes == -1) || (num_bytes >= static_cast<ssize_t>(sizeof(buf))) || (num_bytes < 1)) {
    return -1;
  }

  buf[num_bytes] = '\0';
  int64_t value = strtoll(buf, NULL, 10);

  return value;
}

int NetworkPerformanceInterface::NetworkPerformance::network_utilization(NetworkInterface** network_interfaces) const
{
  ifaddrs* addresses;
  ifaddrs* cur_address;

  if (getifaddrs(&addresses) != 0) {
    return OS_ERR;
  }

  NetworkInterface* ret = NULL;
  for (cur_address = addresses; cur_address != NULL; cur_address = cur_address->ifa_next) {
    if ((cur_address->ifa_addr == NULL) || (cur_address->ifa_addr->sa_family != AF_PACKET)) {
      continue;
    }

    int64_t bytes_in = read_counter(cur_address->ifa_name, "rx_bytes");
    int64_t bytes_out = read_counter(cur_address->ifa_name, "tx_bytes");

    NetworkInterface* cur = new NetworkInterface(cur_address->ifa_name, bytes_in, bytes_out, ret);
    ret = cur;
  }

  freeifaddrs(addresses);
  *network_interfaces = ret;

  return OS_OK;
}

NetworkPerformanceInterface::NetworkPerformanceInterface() {
  _impl = NULL;
}

NetworkPerformanceInterface::~NetworkPerformanceInterface() {
  if (_impl != NULL) {
    delete _impl;
  }
}

bool NetworkPerformanceInterface::initialize() {
  _impl = new NetworkPerformanceInterface::NetworkPerformance();
  return _impl->initialize();
}

int NetworkPerformanceInterface::network_utilization(NetworkInterface** network_interfaces) const {
  return _impl->network_utilization(network_interfaces);
}
