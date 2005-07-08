/*
 * misc.c	Various miscellaneous functions.
 *
 * Version:	$Id$
 *
 *   This library is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU Lesser General Public
 *   License as published by the Free Software Foundation; either
 *   version 2.1 of the License, or (at your option) any later version.
 *
 *   This library is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 *   Lesser General Public License for more details.
 *
 *   You should have received a copy of the GNU Lesser General Public
 *   License along with this library; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307, USA
 *
 * Copyright 2000  The FreeRADIUS server project
 */

static const char rcsid[] =
"$Id$";

#include	"autoconf.h"

#include	<stdio.h>
#include	<sys/types.h>
#include	<sys/socket.h>
#include	<netinet/in.h>
#include	<arpa/inet.h>

#include	<stdlib.h>
#include	<string.h>
#include	<netdb.h>
#include	<ctype.h>
#include	<sys/file.h>
#include	<fcntl.h>
#include	<unistd.h>

#include	"missing.h"
#include	"libradius.h"

int		librad_dodns = 0;
int		librad_debug = 0;

/*
 *	Return a printable host name (or IP address in dot notation)
 *	for the supplied IP address.
 */
char *ip_hostname(char *buf, size_t buflen, uint32_t ipaddr)
{
	struct		hostent *hp;
#ifdef GETHOSTBYADDRRSTYLE
#if (GETHOSTBYADDRRSTYLE == SYSVSTYLE) || (GETHOSTBYADDRRSTYLE == GNUSTYLE)
	char buffer[2048];
	struct hostent result;
	int error;
#endif
#endif

	/*
	 *	No DNS: don't look up host names
	 */
	if (librad_dodns == 0) {
		ip_ntoa(buf, ipaddr);
		return buf;
	}

#ifdef GETHOSTBYADDRRSTYLE
#if GETHOSTBYADDRRSTYLE == SYSVSTYLE
	hp = gethostbyaddr_r((char *)&ipaddr, sizeof(struct in_addr), AF_INET, &result, buffer, sizeof(buffer), &error);
#elif GETHOSTBYADDRRSTYLE == GNUSTYLE
	if (gethostbyaddr_r((char *)&ipaddr, sizeof(struct in_addr),
			    AF_INET, &result, buffer, sizeof(buffer),
			    &hp, &error) != 0) {
		hp = NULL;
	}
#else
	hp = gethostbyaddr((char *)&ipaddr, sizeof(struct in_addr), AF_INET);
#endif
#else
	hp = gethostbyaddr((char *)&ipaddr, sizeof(struct in_addr), AF_INET);
#endif
	if ((hp == NULL) ||
	    (strlen((char *)hp->h_name) >= buflen)) {
		ip_ntoa(buf, ipaddr);
		return buf;
	}

	strNcpy(buf, (char *)hp->h_name, buflen);
	return buf;
}


/*
 *	Return an IP address from a host
 *	name or address in dot notation.
 */
uint32_t ip_getaddr(const char *host)
{
	struct hostent	*hp;
	uint32_t	 a;
#ifdef GETHOSTBYNAMERSTYLE
#if (GETHOSTBYNAMERSTYLE == SYSVSTYLE) || (GETHOSTBYNAMERSTYLE == GNUSTYLE)
	struct hostent result;
	int error;
	char buffer[2048];
#endif
#endif

	if ((a = ip_addr(host)) != htonl(INADDR_NONE))
		return a;

#ifdef GETHOSTBYNAMERSTYLE
#if GETHOSTBYNAMERSTYLE == SYSVSTYLE
	hp = gethostbyname_r(host, &result, buffer, sizeof(buffer), &error);
#elif GETHOSTBYNAMERSTYLE == GNUSTYLE
	if (gethostbyname_r(host, &result, buffer, sizeof(buffer),
			    &hp, &error) != 0) {
		return htonl(INADDR_NONE);
	}
#else
	hp = gethostbyname(host);
#endif
#else
	hp = gethostbyname(host);
#endif
	if (hp == NULL) {
		return htonl(INADDR_NONE);
	}

	/*
	 *	Paranoia from a Bind vulnerability.  An attacker
	 *	can manipulate DNS entries to change the length of the
	 *	address.  If the length isn't 4, something's wrong.
	 */
	if (hp->h_length != 4) {
		return htonl(INADDR_NONE);
	}

	memcpy(&a, hp->h_addr, sizeof(uint32_t));
	return a;
}


/*
 *	Return an IP address in standard dot notation
 *
 *	FIXME: DELETE THIS
 */
const char *ip_ntoa(char *buffer, uint32_t ipaddr)
{
	ipaddr = ntohl(ipaddr);

	sprintf(buffer, "%d.%d.%d.%d",
		(ipaddr >> 24) & 0xff,
		(ipaddr >> 16) & 0xff,
		(ipaddr >>  8) & 0xff,
		(ipaddr      ) & 0xff);
	return buffer;
}


/*
 *	Return an IP address from
 *	one supplied in standard dot notation.
 *
 *	FIXME: DELETE THIS
 */
uint32_t ip_addr(const char *ip_str)
{
	struct in_addr	in;

	if (inet_aton(ip_str, &in) == 0)
		return htonl(INADDR_NONE);
	return in.s_addr;
}


/*
 *	Like strncpy, but always adds \0
 */
char *strNcpy(char *dest, const char *src, int n)
{
	char *p = dest;

	while ((n > 1) && (*src)) {
		*(p++) = *(src++);

		n--;
	}
	*p = '\0';

	return dest;
}

/*
 * Lowercase a string
 */
void rad_lowercase(char *str) {
	char *s;

	for (s=str; *s; s++)
		if (isupper((int) *s)) *s = tolower((int) *s);
}

/*
 * Remove spaces from a string
 */
void rad_rmspace(char *str) {
	char *s = str;
	char *ptr = str;

  while(ptr && *ptr!='\0') {
    while(isspace((int) *ptr))
      ptr++;
    *s = *ptr;
    ptr++;
    s++;
  }
  *s = '\0';
}

/*
 *	Internal wrapper for locking, to minimize the number of ifdef's
 *
 *	Lock an fd, prefer lockf() over flock()
 */
int rad_lockfd(int fd, int lock_len)
{
#if defined(F_LOCK) && !defined(BSD)
	return lockf(fd, F_LOCK, lock_len);
#elif defined(LOCK_EX)
	return flock(fd, LOCK_EX);
#else
	struct flock fl;
	fl.l_start = 0;
	fl.l_len = lock_len;
	fl.l_pid = getpid();
	fl.l_type = F_WRLCK;
	fl.l_whence = SEEK_CUR;
	return fcntl(fd, F_SETLKW, (void *)&fl);
#endif
}

/*
 *	Internal wrapper for locking, to minimize the number of ifdef's
 *
 *	Lock an fd, prefer lockf() over flock()
 *	Nonblocking version.
 */
int rad_lockfd_nonblock(int fd, int lock_len)
{
#if defined(F_LOCK) && !defined(BSD)
	return lockf(fd, F_TLOCK, lock_len);
#elif defined(LOCK_EX)
	return flock(fd, LOCK_EX | LOCK_NB);
#else
	struct flock fl;
	fl.l_start = 0;
	fl.l_len = lock_len;
	fl.l_pid = getpid();
	fl.l_type = F_WRLCK;
	fl.l_whence = SEEK_CUR;
	return fcntl(fd, F_SETLK, (void *)&fl);
#endif
}

/*
 *	Internal wrapper for unlocking, to minimize the number of ifdef's
 *	in the source.
 *
 *	Unlock an fd, prefer lockf() over flock()
 */
int rad_unlockfd(int fd, int lock_len)
{
#if defined(F_LOCK) && !defined(BSD)
	return lockf(fd, F_ULOCK, lock_len);
#elif defined(LOCK_EX)
	return flock(fd, LOCK_UN);
#else
	struct flock fl;
	fl.l_start = 0;
	fl.l_len = lock_len;
	fl.l_pid = getpid();
	fl.l_type = F_WRLCK;
	fl.l_whence = SEEK_CUR;
	return fcntl(fd, F_UNLCK, (void *)&fl);
#endif
}

/*
 *	Return an interface-id in standard colon notation
 */
char *ifid_ntoa(char *buffer, size_t size, uint8_t *ifid)
{
	snprintf(buffer, size, "%x:%x:%x:%x",
		 (ifid[0] << 8) + ifid[1], (ifid[2] << 8) + ifid[3],
		 (ifid[4] << 8) + ifid[5], (ifid[6] << 8) + ifid[7]);
	return buffer;
}


/*
 *	Return an interface-id from
 *	one supplied in standard colon notation.
 */
uint8_t *ifid_aton(const char *ifid_str, uint8_t *ifid)
{
	static const char xdigits[] = "0123456789abcdef";
	const char *p, *pch;
	int num_id = 0, val = 0, idx = 0;

	for (p = ifid_str; ; ++p) {
		if (*p == ':' || *p == '\0') {
			if (num_id <= 0)
				return NULL;

			/*
			 *	Drop 'val' into the array.
			 */
			ifid[idx] = (val >> 8) & 0xff;
			ifid[idx + 1] = val & 0xff;
			if (*p == '\0') {
				/*
				 *	Must have all entries before
				 *	end of the string.
				 */
				if (idx != 6)
					return NULL;
				break;
			}
			val = 0;
			num_id = 0;
			if ((idx += 2) > 6)
				return NULL;
		} else if ((pch = strchr(xdigits, tolower(*p))) != NULL) {
			if (++num_id > 4)
				return NULL;
			/*
			 *	Dumb version of 'scanf'
			 */
			val <<= 4;
			val |= (pch - xdigits);
		} else
			return NULL;
	}
	return ifid;
}


#ifndef HAVE_INET_PTON
static int inet_pton4(const char *src, struct in_addr *dst)
{
	int octet;
	unsigned int num;
	const char *p, *off;
	uint8_t tmp[4];
	static const char digits[] = "0123456789";
	
	octet = 0;
	p = src;
	while (1) {
		num = 0;
		while (*p && ((off = strchr(digits, *p)) != NULL)) {
			num *= 10;
			num += (off - digits);
			
			if (num > 255) return 0;
			
			p++;
		}
		if (!*p) break;
		
		/*
		 *	Not a digit, MUST be a dot, else we
		 *	die.
		 */
		if (*p != '.') {
			return 0;
		}

		tmp[octet++] = num;
		p++;
	}
	
	/*
	 *	End of the string.  At the fourth
	 *	octet is OK, anything else is an
	 *	error.
	 */
	if (octet != 3) {
		return 0;
	}
	tmp[3] = num;
	
	memcpy(dst, &tmp, sizeof(tmp));
	return 1;
}


/* int
 * inet_pton6(src, dst)
 *	convert presentation level address to network order binary form.
 * return:
 *	1 if `src' is a valid [RFC1884 2.2] address, else 0.
 * notice:
 *	(1) does not touch `dst' unless it's returning 1.
 *	(2) :: in a full address is silently ignored.
 * credit:
 *	inspired by Mark Andrews.
 * author:
 *	Paul Vixie, 1996.
 */
static int
inet_pton6(const char *src, unsigned char *dst)
{
	static const char xdigits_l[] = "0123456789abcdef",
			  xdigits_u[] = "0123456789ABCDEF";
	u_char tmp[IN6ADDRSZ], *tp, *endp, *colonp;
	const char *xdigits, *curtok;
	int ch, saw_xdigit;
	u_int val;

	memset((tp = tmp), 0, IN6ADDRSZ);
	endp = tp + IN6ADDRSZ;
	colonp = NULL;
	/* Leading :: requires some special handling. */
	if (*src == ':')
		if (*++src != ':')
			return (0);
	curtok = src;
	saw_xdigit = 0;
	val = 0;
	while ((ch = *src++) != '\0') {
		const char *pch;

		if ((pch = strchr((xdigits = xdigits_l), ch)) == NULL)
			pch = strchr((xdigits = xdigits_u), ch);
		if (pch != NULL) {
			val <<= 4;
			val |= (pch - xdigits);
			if (val > 0xffff)
				return (0);
			saw_xdigit = 1;
			continue;
		}
		if (ch == ':') {
			curtok = src;
			if (!saw_xdigit) {
				if (colonp)
					return (0);
				colonp = tp;
				continue;
			}
			if (tp + INT16SZ > endp)
				return (0);
			*tp++ = (u_char) (val >> 8) & 0xff;
			*tp++ = (u_char) val & 0xff;
			saw_xdigit = 0;
			val = 0;
			continue;
		}
		if (ch == '.' && ((tp + INADDRSZ) <= endp) &&
		    inet_pton4(curtok, tp) > 0) {
			tp += INADDRSZ;
			saw_xdigit = 0;
			break;	/* '\0' was seen by inet_pton4(). */
		}
		return (0);
	}
	if (saw_xdigit) {
		if (tp + INT16SZ > endp)
			return (0);
		*tp++ = (u_char) (val >> 8) & 0xff;
		*tp++ = (u_char) val & 0xff;
	}
	if (colonp != NULL) {
		/*
		 * Since some memmove()'s erroneously fail to handle
		 * overlapping regions, we'll do the shift by hand.
		 */
		const int n = tp - colonp;
		int i;

		for (i = 1; i <= n; i++) {
			endp[- i] = colonp[n - i];
			colonp[n - i] = 0;
		}
		tp = endp;
	}
	if (tp != endp)
		return (0);
	/* bcopy(tmp, dst, IN6ADDRSZ); */
	memcpy(dst, tmp, IN6ADDRSZ);
	return (1);
}

/*
 *	Utility function, so that the rest of the server doesn't
 *	have ifdef's around IPv6 support
 */
int inet_pton(int af, const char *src, void *dst)
{
	if (af == AF_INET) {
		return inet_pton4(src, dst);
	}

	if (af == AF_INET6) {
		return inet_pton6(src, dst);
	}

	return -1;
}
#endif


#ifndef HAVE_INET_NTOP
/*
 *	Utility function, so that the rest of the server doesn't
 *	have ifdef's around IPv6 support
 */
const char *inet_ntop(int af, const void *src, char *dst, size_t cnt)
{
	if (af == AF_INET) {
		const uint8_t *ipaddr = src;

		if (cnt <= INET_ADDRSTRLEN) return NULL;
		
		snprintf(dst, cnt, "%d.%d.%d.%d",
			 ipaddr[0], ipaddr[1],
			 ipaddr[2], ipaddr[3]);
		return dst;
	}

	/*
	 *	If the system doesn't define this, we define it
	 *	in missing.h
	 */
	if (af == AF_INET6) {
		const struct in6_addr *ipaddr = src;
		
		if (cnt <= INET6_ADDRSTRLEN) return NULL;

		snprintf(dst, cnt, "%x:%x:%x:%x:%x:%x:%x:%x",
			 (ipaddr->s6_addr[0] << 8) | ipaddr->s6_addr[1],
			 (ipaddr->s6_addr[2] << 8) | ipaddr->s6_addr[3],
			 (ipaddr->s6_addr[4] << 8) | ipaddr->s6_addr[5],
			 (ipaddr->s6_addr[6] << 8) | ipaddr->s6_addr[7],
			 (ipaddr->s6_addr[8] << 8) | ipaddr->s6_addr[9],
			 (ipaddr->s6_addr[10] << 8) | ipaddr->s6_addr[11],
			 (ipaddr->s6_addr[12] << 8) | ipaddr->s6_addr[13],
			 (ipaddr->s6_addr[14] << 8) | ipaddr->s6_addr[15]);
		return dst;
	}

	return NULL;		/* don't support IPv6 */
}
#endif


/*
 *	Wrappers for IPv4/IPv6 host to IP address lookup.
 *	This API returns only one IP address, of the specified
 *	address family, or the first address (of whatever family),
 *	if AF_UNSPEC is used.
 */
int ip_hton(const char *src, int af, lrad_ipaddr_t *dst)
{
	int error;
	struct addrinfo hints, *ai = NULL, *res = NULL;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = af;

	if ((error = getaddrinfo(src, NULL, &hints, &res)) != 0) {
		librad_log("ip_nton: %s", gai_strerror(error));
		return -1;
	}

	for (ai = res; ai; ai = ai->ai_next) {
		if ((af == ai->ai_family) || (af == AF_UNSPEC))
			break;
	}

	if (!ai) {
		librad_log("ip_hton failed to find requested information for host %.100s", src);
		freeaddrinfo(ai);
		return -1;
	}

	switch (ai->ai_family) {
	case AF_INET :
		dst->af = AF_INET;
		memcpy(&dst->ipaddr, 
		       &((struct sockaddr_in*)ai->ai_addr)->sin_addr, 
		       sizeof(struct in_addr));
		break;
		
	case AF_INET6 :
		dst->af = AF_INET6;
		memcpy(&dst->ipaddr, 
		       &((struct sockaddr_in6*)ai->ai_addr)->sin6_addr, 
		       sizeof(struct in6_addr));
		break;
		
		/* Flow should never reach here */
	case AF_UNSPEC :
	default :
		librad_log("ip_hton found unusable information for host %.100s", src);
		freeaddrinfo(ai);
		return -1;
	}
	
	freeaddrinfo(ai);
	return 0;
}

/*
 *	Look IP addreses up, and print names (depending on DNS config)
 */
const char *ip_ntoh(const lrad_ipaddr_t *src, char *dst, size_t cnt)
{
	struct sockaddr_storage ss;
	struct sockaddr_in  *s4;
	int error, len;

	memset(&ss, 0, sizeof(ss));
        switch (src->af) {
        case AF_INET :
                s4 = (struct sockaddr_in *)&ss;
                len    = sizeof(struct sockaddr_in);
                s4->sin_family = AF_INET;
                s4->sin_port = 0;
                memcpy(&s4->sin_addr, &src->ipaddr.ip4addr, 4);
                break;

#ifdef HAVE_STRUCT_SOCKADDR_IN6
        case AF_INET6 :
		{
		struct sockaddr_in6 *s6;

                s6 = (struct sockaddr_in6 *)&ss;
                len    = sizeof(struct sockaddr_in6);
                s6->sin6_family = AF_INET6;
                s6->sin6_flowinfo = 0;
                s6->sin6_port = 0;
                memcpy(&s6->sin6_addr, &src->ipaddr.ip6addr, 16);
                break;
		}
#endif

        default :
                return NULL;
        }

	if ((error = getnameinfo(&ss, len, dst, cnt, NULL, 0,
			 NI_NUMERICHOST | NI_NUMERICSERV)) != 0) {
		librad_log("ip_ntoh: %s", gai_strerror(error));
		return NULL;
	}
	return dst;
}


static const char *hextab = "0123456789abcdef";

/*
 *	hex2bin
 *
 *	We allow: hex == bin
 */
int lrad_hex2bin(const unsigned char *hex, unsigned char *bin, int len)
{
	int i;
	char *c1, *c2;

	for (i = 0; i < len; i++) {
		if(!(c1 = memchr(hextab, tolower((int) hex[i << 1]), 16)) ||
		   !(c2 = memchr(hextab, tolower((int) hex[(i << 1) + 1]), 16)))
			break;
                 bin[i] = ((c1-hextab)<<4) + (c2-hextab);
	}

	return i;
}


/*
 *	bin2hex
 *
 *	If the output buffer isn't long enough, we have a buffer overflow.
 */
void lrad_bin2hex(const unsigned char *bin, unsigned char *hex, int len)
{
	int i;

	for (i = 0; i < len; i++) {
		hex[0] = hextab[((*bin) >> 4) & 0x0f];
		hex[1] = hextab[*bin & 0x0f];
		hex += 2;
		bin++;
	}
	*hex = '\0';
	return;
}


#define FNV_MAGIC_INIT (0x811c9dc5)
#define FNV_MAGIC_PRIME (0x01000193)

/*
 *	A fast hash function.  For details, see:
 *
 *	http://www.isthe.com/chongo/tech/comp/fnv/
 *
 *	Which also includes public domain source.  We've re-written
 *	it here for our purposes.
 */
uint32_t lrad_hash(const void *data, size_t size)
{
	const uint8_t *p = data;
	const uint8_t *q = p + size;
	uint32_t      hash = FNV_MAGIC_INIT;

	/*
	 *	FNV-1 hash each octet in the buffer
	 */
	while (p != q) {
		/*
		 *	Multiple by 32-bit magic FNV prime, mod 2^32
		 */
		hash *= FNV_MAGIC_PRIME;
#if 0
		/*
		 *	Potential optimization.
		 */
		hash += (hash<<1) + (hash<<4) + (hash<<7) + (hash<<8) + (hash<<24);
#endif
		/*
		 *	XOR the 8-bit quantity into the bottom of
		 *	the hash.
		 */
		hash ^= (uint32_t) (*p++);
    }

    return hash;
}

/*
 *	Continue hashing data.
 */
uint32_t lrad_hash_update(const void *data, size_t size, uint32_t hash)
{
	const uint8_t *p = data;
	const uint8_t *q = p + size;

	while (p != q) {
		hash *= FNV_MAGIC_PRIME;
		hash ^= (uint32_t) (*p++);
    }

    return hash;

}

/*
 *	Return a "folded" hash, where the lower "bits" are the
 *	hash, and the upper bits are zero.
 *
 *	If you need a non-power-of-two hash, cope.
 */
uint32_t lrad_hash_fold(uint32_t hash, int bits)
{
	int count;
	uint32_t result;

	if ((bits <= 0) || (bits >= 32)) return hash;

	result = hash;

	/*
	 *	Never use the same bits twice in an xor.
	 */
	for (count = 0; count < 32; count += bits) {
		hash >>= bits;
		result ^= hash;
	}

	return result & (((uint32_t) (1 << bits)) - 1);
}
