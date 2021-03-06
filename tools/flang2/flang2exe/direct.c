/*
 * Copyright (c) 2017, NVIDIA CORPORATION.  All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

/** \file
 * \brief Directive/pragma support modules
 */

#include "gbldefs.h"
#include "error.h"
#include "global.h"
#include "symtab.h"
#include "direct.h"

#if DEBUG
static void dmp_dirset(DIRSET *);
static void dmp_lpprg(int);
#define TR0(s)         \
  if (DBGBIT(1, 1024)) \
    fprintf(gbl.dbgfil, s);
#define TR1(s, a)      \
  if (DBGBIT(1, 1024)) \
    fprintf(gbl.dbgfil, s, a);
#define TR2(s, a, b)   \
  if (DBGBIT(1, 1024)) \
    fprintf(gbl.dbgfil, s, a, b);
#define TR3(s, a, b, c) \
  if (DBGBIT(1, 1024))  \
    fprintf(gbl.dbgfil, s, a, b, c);

#else
#define TR0(s)
#define TR1(s, a)
#define TR2(s, a, b)
#define TR3(s, a, b, c)

#endif
#include "x86.h"

DIRECT direct;

/** \brief Initialize directive structure
 *
 * Initialize directive structure which is global for the
 * source file and the structure which is used per routine.
 * must be called just once for the compilation and after all of the
 * command line options have been processed.
 *
 * The initial values of the global structure are extracted from
 * the command line options.  The routine structure is initialized
 * with values from the global structure.
 */
void
direct_init(void)
{

/* Set/clear any xbits for which the command line processing has no
 * effect
 */
  flg.x[8] |= 0x8; /* disable global reg assignment */

  store_dirset(&direct.gbl);

  direct.rou = direct.gbl;
  direct.loop = direct.gbl;
  direct.rou_begin = direct.gbl;

  direct.loop_flag = FALSE; /* seen pragma with loop scope */
  direct.in_loop = FALSE;   /* in loop with pragmas */
  direct.carry_fwd = FALSE;

  direct.avail = 0;
  NEW(direct.stgb, DIRSET, (direct.size = 16));

  direct.lpg.avail = 1;

  NEW(direct.lpg.stgb, LPPRG, (direct.lpg.size = 16));
  BZERO(direct.lpg.stgb, LPPRG, 1);
  direct.lpg_stk.top = 0;
  NEW(direct.lpg_stk.stgb, LPG_STK, (direct.lpg_stk.size = 8));

}

void
direct_fini()
{
  if (direct.stgb) {
    FREE(direct.stgb);
    direct.avail = direct.size = 0;
  }
  if (direct.lpg.stgb) {
    FREE(direct.lpg.stgb);
    direct.lpg.avail = direct.lpg.size = 0;
  }
  if (direct.lpg_stk.stgb) {
    FREE(direct.lpg_stk.stgb);
    direct.lpg_stk.top = direct.lpg_stk.size = 0;
  }
} /* direct_fini */

/** \brief Re-initialize the routine structure
 *
 * Must be called after the end of a function is processed by semant and
 * before the next function is parsed. For C, this is when the END ilm is
 * written for a function.  For Fortran, this is after a subprogram has been
 * processed by all phases of the compiler.
 *
 * For C, process any symbol-/variable- related pragmas which may have
 * occurred.  Also, save the index into the lpprg table which is the beginning
 * of the function's lpprg segment; mark the end of the segment with an entry
 * whose beg_line is -1.
 */
void
direct_rou_end(void)
{
/* CPLUS also needs to save routine's structure: */
  direct.lpg.avail = 1;

  direct.rou = direct.gbl;
  direct.loop = direct.gbl;
  direct.rou_begin = direct.gbl;
  direct.carry_fwd = FALSE;

}

void
direct_loop_enter(void)
{
  if (direct.loop_flag || (direct.carry_fwd && !direct.in_loop)) {
    push_lpprg(gbl.lineno);
  }

}

/** \brief Re-initialize the loop structure
 *
 * Must be called after the end of a loop is processed by semant for which
 * loop-scoped pragmas/directives apply.
 */
void
direct_loop_end(int beg_line, int end_line)
{
  int i;
  LPPRG *lpprg;

  if (!direct.in_loop)
    return;
  i = direct.lpg_stk.stgb[direct.lpg_stk.top].dirx;
  lpprg = direct.lpg.stgb + i;
  if (lpprg->beg_line != beg_line)
    return;

/***** pop_lpprg *****/

  TR1("---pop_lpprg: top %d,", direct.lpg_stk.top);
  direct.lpg_stk.top--;
  TR1(" lpprg %d,", i);
  lpprg = direct.lpg.stgb + i;

  lpprg->end_line = end_line;
  TR2(" beg %d, end %d\n", lpprg->beg_line, lpprg->end_line);

  direct.loop = direct.rou;

  if (direct.lpg_stk.top == 0) {
    direct.loop_flag = FALSE;
    direct.in_loop = FALSE;
  } else if (XBIT(59, 1)) {
    direct.loop =
        direct.lpg.stgb[direct.lpg_stk.stgb[direct.lpg_stk.top].dirx].dirset;
  } else {
    /*
     * propagate selected directives/pragmas to all nested
     */
    i = direct.lpg_stk.stgb[direct.lpg_stk.top].dirx;
    direct.loop.depchk = direct.lpg.stgb[i].dirset.depchk;
  }
#if DEBUG
  if (DBGBIT(1, 512))
    dmp_lpprg(direct.lpg_stk.stgb[direct.lpg_stk.top + 1].dirx);
#endif

}

typedef struct xf_tag {
  char *fn; /* name of function */
  int x;    /* which xflag */
  int v;    /* value of xflag */
  struct xf_tag *next;
} XF;

static XF *xf_p = NULL; /* list of function -x ... */
static XF *yf_p = NULL; /* list of function -y ... */

void
direct_xf(char *fn, int x, int v)
{
  XF *xfp;
  /*printf("-xf %s %d 0x%x\n", fn, x, v);*/
  xfp = (XF *)getitem(8, sizeof(XF));
  xfp->next = xf_p;
  xf_p = xfp;
  xfp->fn = fn;
  xfp->x = x;
  xfp->v = v;
}

void
direct_yf(char *fn, int x, int v)
{
  XF *xfp;
  /*printf("-yf %s %d 0x%x\n", fn, x, v);*/
  xfp = (XF *)getitem(8, sizeof(XF));
  xfp->next = yf_p;
  yf_p = xfp;
  xfp->fn = fn;
  xfp->x = x;
  xfp->v = v;
}

/** \brief Load direct.rou for the current function
 *
 * Called after the parse phase is complete; called once per function.  For C
 * this means the call occurs during expand when it sees the ENTRY ilm; for
 * Fortran, this is at the beginning of expand (in main).
 *
 * DIRSET direct.rou_begin represents the state of the directives/pragmas at
 * the beginning of the function.
 *
 * \param func - symbol of current function
 */
void
direct_rou_load(int func)
{
  DIRSET *currdir;
  XF *xfp;
  char *fnp;

  currdir = &direct.rou_begin;

  load_dirset(currdir);

  fnp = SYMNAME(gbl.currsub);
  for (xfp = xf_p; xfp != NULL; xfp = xfp->next) {
    if (strcmp(xfp->fn, fnp) == 0) {
      /*printf("-xf %s %d 0x%x\n", xfp->fn, xfp->x, xfp->v);*/
      set_xflag(xfp->x, xfp->v);
      currdir->x[xfp->x] = flg.x[xfp->x];
    }
  }
  for (xfp = yf_p; xfp != NULL; xfp = xfp->next) {
    if (strcmp(xfp->fn, fnp) == 0) {
      /*printf("-yf %s %d 0x%x\n", xfp->fn, xfp->x, xfp->v);*/
      set_yflag(xfp->x, xfp->v);
      currdir->x[xfp->x] = flg.x[xfp->x];
    }
  }

  /*
   * the optimizer doesn't handle assigned goto's correctly.
   * (Doesn't know where to put loop exit code if you assign
   * goto out of loop)
   */
  if ((gbl.asgnlbls == -1) && (flg.opt >= 2)) {
    error(127, 1, 0, SYMNAME(gbl.currsub), "due to assigned goto");
    currdir->opt = flg.opt = 1;
    currdir->vect = flg.vect = 0;
  }
  if (gbl.vfrets) {
    /*
     * temporarily disable optimizations not correctly
     * handle if variable functions occur.
     */
    if (flg.opt >= 2) {
      error(127, 1, 0, SYMNAME(gbl.currsub), "due to < > in FORMAT");
      currdir->opt = flg.opt = 1;
      currdir->vect = flg.vect = 0;
    }
    flg.x[8] |= 0x8; /* no globalregs at opt 1 */
  }

#if DEBUG
  if (DBGBIT(1, 256)) {
    fprintf(gbl.dbgfil, "---dirset for func ");
    fprintf(gbl.dbgfil, "%s\n", SYMNAME(func));
    dmp_dirset(currdir);
  }
#endif

  if (gbl.multiversion == 0) {
    set_mach(&mach, direct.rou_begin.tpvalue[0]);
  } else {
    set_mach(&mach, direct.rou_begin.tpvalue[gbl.multiversion - 1]);
  }

}

void
direct_rou_setopt(int func, int opt)
{
  DIRSET *currdir;
  currdir = &direct.rou_begin;
  flg.opt = opt;
  currdir->opt = opt;
}

void
load_dirset(DIRSET *currdir)
{
  flg.depchk = currdir->depchk;
  flg.opt = currdir->opt;
  flg.vect = currdir->vect;
  BCOPY(flg.tpvalue, currdir->tpvalue, int, TPNVERSION);
  BCOPY(flg.x, currdir->x, int, (INT)sizeof(flg.x) / sizeof(int));
#if DEBUG
  if (DBGBIT(1, 2048))
    dmp_dirset(currdir);
#endif

}

void
store_dirset(DIRSET *currdir)
{
  currdir->depchk = flg.depchk;
  currdir->opt = flg.opt;
  currdir->vect = flg.vect;
  BCOPY(currdir->tpvalue, flg.tpvalue, int, TPNVERSION);
  BCOPY(currdir->x, flg.x, int, (INT)sizeof(flg.x) / sizeof(int));

}

/** \brief OPTIONS statement processed (by scan via semant)
 *
 * These only affect
 * what happens in semant for the 'next' routine.  alter any dirset
 * values which can be altered by OPTIONS.
 *
 * \param restore TRUE if called when restoring effects of OPTIONS
 */
void
dirset_options(LOGICAL restore)
{
  if (restore)
    direct.rou_begin.x[70] = direct.gbl.x[70];
  else
    direct.rou_begin.x[70] = flg.x[70];

}

#if DEBUG
static void
dmp_dirset(DIRSET *currdir)
{
#define _FNO(s) ((s) ? "" : "no")
#define _TNO(s) ((s) ? "no" : "")
  fprintf(gbl.dbgfil,
          "   opt=%d,%sdepchk,%sassoc,%stransform,%srecog,%sswpipe,%sstream\n",
          currdir->opt, _FNO(currdir->depchk), _TNO(currdir->vect & 0x4),
          _TNO(currdir->x[19] & 0x8), _TNO(currdir->x[19] & 0x10),
          _TNO(currdir->x[19] & 0x20), _TNO(currdir->x[19] & 0x40));
  fprintf(gbl.dbgfil, "   shortloop:%d", currdir->x[35]);
  fprintf(gbl.dbgfil, " %seqvchk", _TNO(currdir->x[19] & 0x1));
  fprintf(gbl.dbgfil,
          "   %slstval,%ssplit,%svintr,%spipei,%sdualopi,%sbounds,%ssse\n",
          _TNO(currdir->x[19] & 0x2), _FNO(currdir->x[19] & 0x4),
          _TNO(currdir->x[34] & 0x8), _FNO(currdir->x[4] & 0x1),
          _FNO(currdir->x[4] & 0x2), _FNO(currdir->x[70] & 0x2),
          _TNO(currdir->x[19] & 0x400));
  fprintf(gbl.dbgfil, "   altcode: vector=%d,swpipe=%d,unroll=%d\n",
          currdir->x[16], currdir->x[17], currdir->x[18]);
  fprintf(gbl.dbgfil, "   %sfunc32, %sframe", _FNO(currdir->x[119] & 0x4),
          _TNO(currdir->x[121] & 0x1));
  fprintf(gbl.dbgfil, " info=%0x", currdir->x[0]);
  fprintf(gbl.dbgfil, "   stripsize:%d", currdir->x[38]);
  if (currdir->x[34] & 0x100000)
    fprintf(gbl.dbgfil, "   nolastdim");
  if (currdir->x[34] & 0x800)
    fprintf(gbl.dbgfil, "   safe_last_val");
  fprintf(gbl.dbgfil, "\n");
  fprintf(gbl.dbgfil, "   %sconcur,%sinvarif,%sunroll=c,%sunroll=n,",
          _TNO(currdir->x[34] & (0x20 | 0x10)), _TNO(currdir->x[19] & 0x80),
          _TNO(currdir->x[11] & 0x1), _TNO(currdir->x[11] & 0x2));
  fprintf(gbl.dbgfil, "unroll=c:%d,unroll=n:%d", currdir->x[9], currdir->x[10]);
  fprintf(gbl.dbgfil, "\n");
}

static void
dmp_lpprg(int i)
{
  LPPRG *p;

  p = direct.lpg.stgb + i;
  fprintf(gbl.dbgfil, "---dirset (%4d) for loop, lines %d, %d\n", i,
          p->beg_line, p->end_line);
  dmp_dirset(&p->dirset);
}
#endif

static FILE *dirfil;
static int ilmlinenum = 0;
#define MAXLINELEN 4096
static char line[MAXLINELEN];
static int read_line(void);
static int rd_dir(DIRSET *);

int
direct_import(FILE *ff)
{
  int ret;
  int i;
  int idx;
  LPPRG *lpprg;

  ilmlinenum = 0;
  dirfil = ff;

  /* read size of the lpg table */
  if (read_line())
    goto err;
  ret = sscanf(line, "A:%d", &direct.lpg.avail);
  if (ret != 1)
    goto err;
  NEEDB(direct.lpg.avail, direct.lpg.stgb, LPPRG, direct.lpg.size,
        direct.lpg.avail + 8);

  /* read routine directives */
  if (read_line())
    goto err; /* rou: line */
  if (line[0] != 'r')
    goto err;
  direct.rou_begin = direct.gbl;
  if (rd_dir(&direct.rou_begin))
    goto err;

  /* read the loop directives */
  for (i = 1; i < direct.lpg.avail; i++) {
    lpprg = direct.lpg.stgb + i;

    if (read_line())
      goto err; /* idx: line */
    ret = sscanf(line, "%d: ", &idx);
    if (ret != 1)
      goto err;
    if (i != idx)
      goto err;

    if (read_line())
      goto err; /* b:lineno e:lineno */
    ret = sscanf(line, "b:%d e:%d", &lpprg->beg_line, &lpprg->end_line);
    if (ret != 2)
      goto err;

    lpprg->dirset = direct.rou_begin;
    if (rd_dir(&lpprg->dirset))
      goto err;
#if DEBUG
    if (DBGBIT(1, 512))
      dmp_lpprg(i);
#endif
  }

  return ilmlinenum;
err:
  printf("DIRECTIVES error\n");
  return ilmlinenum;
}

static int
read_line(void)
{
  char *ret;
  ret = fgets(line, MAXLINELEN - 1, dirfil);
  ++ilmlinenum;
  if (ret == NULL)
    return 1;
  return 0;
} /* read_line */

static int
rd_dir(DIRSET *dd)
{
  int ret;
  int v;
  int change;
  int idx;

#undef ST_VAL
#undef ST_BV
#undef UADDR
#define ST_VAL(m) \
  if (change)     \
  dd->m = v
#define ST_BV(m) \
  if (change)    \
  dd->m = (v & change) | (dd->m & ~change)
#define UADDR(x) (unsigned int *) & x

  while (TRUE) {
    /* read input line */
    if (read_line())
      return 1;
    switch (line[0]) {
    case 'z':
      return 0;
    case 'o': /* read opt line */
      ret = sscanf(line, "o:%x %x", UADDR(change), UADDR(v));
      if (ret != 2)
        return 1;
      {
        ST_VAL(opt);
      }
      break;
    case 'v': /* read vect line */
      ret = sscanf(line, "v:%x %x", UADDR(change), UADDR(v));
      if (ret != 2)
        return 1;
      if (dd != &direct.rou_begin) {
        ST_BV(vect);
      } else {
        ST_VAL(vect);
      }
      break;
    case 'd': /* read depchk line */
      ret = sscanf(line, "d:%x %x", UADDR(change), UADDR(v));
      if (ret != 2)
        return 1;
      {
        ST_VAL(depchk);
      }
      break;
    case 'x':
      /* read x flag.  The line is of the form:
       *   x<n>:change new [idx:change new ]...
       * <n> is in decimal; change & new are in hex.
       */
      ret = sscanf(line, "x%d:%x %x", &idx, UADDR(change), UADDR(v));
      if (ret != 3)
        return 1;
      if (dd == &direct.rou_begin) {
        ST_VAL(x[idx]);
      } else if (is_xflag_bit(idx)) {
        ST_BV(x[idx]);
      } else {
        ST_VAL(x[idx]);
      }
      break;
    default:
      return 1;
    }
  }
  return 0;
}
