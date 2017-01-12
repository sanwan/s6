 /* ISC license. */

#include <sys/types.h>
#include <stdint.h>
#include <errno.h>
#include <skalibs/uint32.h>
#include <skalibs/allreadwrite.h>
#include <skalibs/error.h>
#include <skalibs/bytestr.h>
#include <skalibs/tai.h>
#include <skalibs/genalloc.h>
#include <skalibs/djbunix.h>
#include <skalibs/unixmessage.h>
#include <s6/s6-fdholder.h>

int s6_fdholder_getdump (s6_fdholder_t *a, genalloc *g, tain_t const *deadline, tain_t *stamp)
{
  unixmessage_t m  = { .s = "?", .len = 1, .fds = 0, .nfds = 0 } ;
  uint32_t ntot, n ;
  unsigned int oldlen = genalloc_len(s6_fdholder_fd_t, g) ;
  unsigned int i = 0 ;
  int ok ;
  if (!unixmessage_put(&a->connection.out, &m)) return 0 ;
  if (!unixmessage_sender_timed_flush(&a->connection.out, deadline, stamp)) return 0 ;
  if (sanitize_read(unixmessage_timed_receive(&a->connection.in, &m, deadline, stamp)) < 0) return 0 ;
  if (!m.len || m.nfds) return (errno = EPROTO, 0) ;
  if (m.s[0]) return (errno = m.s[0], 0) ;
  if (m.len != 9) return (errno = EPROTO, 0) ;
  uint32_unpack_big(m.s + 1, &n) ;
  uint32_unpack_big(m.s + 5, &ntot) ;
  if (!ntot) return 1 ;
  if (n != 1 + (ntot-1) / UNIXMESSAGE_MAXFDS) return (errno = EPROTO, 0) ;
  ok = genalloc_readyplus(s6_fdholder_fd_t, g, ntot) ;
  
  for (; i < n ; i++)
  {
    if (sanitize_read(unixmessage_timed_receive(&a->connection.in, &m, deadline, stamp)) < 0) goto err ;
    if (genalloc_len(s6_fdholder_fd_t, g) + m.nfds > ntot) goto droperr ;
    if (ok)
    {
      s6_fdholder_fd_t *tab = genalloc_s(s6_fdholder_fd_t, g) + genalloc_len(s6_fdholder_fd_t, g) ;
      unsigned int j = 0 ;
      for (; j < m.nfds ; i++)
      {
        unsigned char thislen ;
        if (m.len < TAIN_PACK + 3) goto droperr ;
        tain_unpack(m.s, &tab[j].limit) ;
        m.s += TAIN_PACK ; m.len -= TAIN_PACK + 1 ;
        thislen = *m.s++ ;
        if (thislen > m.len - 1 || m.s[thislen]) goto droperr ;
        byte_copy(tab[j].id, thislen, m.s) ;
        byte_zero(tab[j].id + thislen, S6_FDHOLDER_ID_SIZE + 1 - thislen) ;
        m.s += (size_t)thislen + 1 ; m.len -= (size_t)thislen + 1 ;
        tab[j].fd = m.fds[j] ;
      }
      genalloc_setlen(s6_fdholder_fd_t, g, genalloc_len(s6_fdholder_fd_t, g) + m.nfds) ;
    }
    else unixmessage_drop(&m) ;
  }

  if (!ok) return (errno = ENOMEM, 0) ;
  return 1 ;

 droperr:
   unixmessage_drop(&m) ;
   errno = EPROTO ;
 err:
  {
    int e = errno ;
    size_t j = genalloc_len(s6_fdholder_fd_t, g) ;
    while (j-- > oldlen) fd_close(genalloc_s(s6_fdholder_fd_t, g)[j].fd) ;
    genalloc_setlen(s6_fdholder_fd_t, g, oldlen) ;
    errno = e ;
  }
  return 0 ;
}
