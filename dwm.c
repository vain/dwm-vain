/* See LICENSE file for copyright and license details.
 *
 * dynamic window manager is designed like any other X client as well. It is
 * driven through handling X events. In contrast to other X clients, a window
 * manager selects for SubstructureRedirectMask on the root window, to receive
 * events about window (dis-)appearance.  Only one X connection at a time is
 * allowed to select for this event mask.
 *
 * The event handlers of dwm are organized in an array which is accessed
 * whenever a new event has been fetched. This allows event dispatching
 * in O(1) time.
 *
 * Each child of the root window is called a client, except windows which have
 * set the override_redirect flag.  Clients are organized in a linked client
 * list on each monitor, the focus history is remembered through a stack list
 * on each monitor. Each client contains a bit array to indicate the tags of a
 * client.
 *
 * Keys and tagging rules are organized as arrays and defined in config.h.
 *
 * To understand everything else, start reading main().
 */
#include <errno.h>
#include <locale.h>
#include <stdarg.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <X11/cursorfont.h>
#include <X11/keysym.h>
#include <X11/XF86keysym.h>
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xproto.h>
#include <X11/Xutil.h>
#include <X11/extensions/Xinerama.h>
#include <X11/extensions/Xfixes.h>
#include <X11/extensions/shape.h>

/* macros */
#define BUTTONMASK              (ButtonPressMask|ButtonReleaseMask)
#define CLEANMASK(mask)         (mask & ~(numlockmask|LockMask) & (ShiftMask|ControlMask|Mod1Mask|Mod2Mask|Mod3Mask|Mod4Mask|Mod5Mask))
#define INTERSECT(x,y,w,h,m)    (MAX(0, MIN((x)+(w),(m)->wx+(m)->ww) - MAX((x),(m)->wx)) \
                               * MAX(0, MIN((y)+(h),(m)->wy+(m)->wh) - MAX((y),(m)->wy)))
#define ISVISIBLE(C)            ((C->tags & C->mon->tagset[C->mon->seltags]))
#define LENGTH(X)               (sizeof X / sizeof X[0])
#define MAX(A, B)               ((A) > (B) ? (A) : (B))
#define MIN(A, B)               ((A) < (B) ? (A) : (B))
#define MOUSEMASK               (BUTTONMASK|PointerMotionMask)
#define WIDTH(X)                ((X)->w + 2 * totalborderpx)
#define HEIGHT(X)               ((X)->h + 2 * totalborderpx + titlepx)
#define TAGMASK                 ((1 << LENGTH(tags)) - 1)
#define TEXTW(X, F)             (textnw(X, strlen(X), &F) + F.height)

/* enums */
enum { CurNormal, CurResize, CurMove, CurLast };        /* cursor */
enum { ColFG, ColBG, ColLast };                         /* color */
enum { NetSupported, NetWMName, NetWMState,
       NetWMFullscreen, NetActiveWindow, NetWMWindowType,
       NetWMWindowTypeDialog, NetClientList, NetLast };     /* EWMH atoms */
enum { WMProtocols, WMDelete, WMState, WMTakeFocus, WMLast }; /* default atoms */
enum { ClkClientWin, ClkRootWin, ClkLast };             /* clicks */
enum BorderType { StateNormal, StateFocused, StateUrgent, StateAuto };

typedef union {
	int i;
	unsigned int ui;
	float f;
	const void *v;
} Arg;

typedef struct {
	unsigned int click;
	unsigned int mask;
	unsigned int button;
	void (*func)(const Arg *arg);
	const Arg arg;
} Button;

typedef struct Monitor Monitor;
typedef struct Client Client;
struct Client {
	char name[256];
	float mina, maxa;
	int x, y, w, h;
	int oldx, oldy, oldw, oldh;
	int basew, baseh, incw, inch, maxw, maxh, minw, minh;
	int bw, oldbw;
	unsigned int tags;
	Bool isfixed, isfloating, isurgent, neverfocus, oldstate, isfullscreen,
	     sizehints;
	Client *next;
	Client *snext;
	Monitor *mon;
	Window win;
};

typedef struct {
	unsigned long norm[ColLast];
	unsigned long sel[ColLast];
	unsigned long urg[ColLast];
	unsigned long infonorm[ColLast];
	unsigned long infosel[ColLast];
	unsigned long linecolor;
	unsigned long baremptycolor;
} ColorInfo;

typedef struct {
	int ascent;
	int descent;
	int height;
	XFontSet set;
	XFontStruct *xfont;
} FontInfo;

typedef struct {
	int x, y, w, h;
	Drawable drawable;
	FontInfo *fi;
	GC gc;
} DC; /* draw context */

typedef struct {
	unsigned int mod;
	KeySym keysym;
	void (*func)(const Arg *);
	const Arg arg;
} Key;

typedef struct {
	const char *symbol;
	void (*arrange)(Monitor *);
} Layout;

struct Monitor {
	char ltsymbol[16];
	float mfact;
	int nmaster;
	int nmaster_dynamic_max;
	int num;
	int by;               /* bar geometry */
	int mx, my, mw, mh;   /* screen size */
	int wx, wy, ww, wh;   /* window area  */
	int lmx, lmy;
	unsigned int seltags;
	unsigned int tagset[2];
	Bool showbar;
	Bool topbar;
	Client *clients;
	Client *sel;
	Client *stack;
	Monitor *next;
	Window barwin;
	const Layout *lt;
	PointerBarrier barrier[4];
};

typedef struct {
	const char *class;
	const char *instance;
	const char *title;
	unsigned int tags;
	Bool isfloating;
	int monitor;
	Bool sizehints;
} Rule;

/* function declarations */
static void applyrules(Client *c);
static Bool applysizehints(Client *c, int *x, int *y, int *w, int *h, Bool interact);
static void arrange(Monitor *m);
static void arrangemon(Monitor *m);
static void attach(Client *c);
static void attachstack(Client *c);
static void buttonpress(XEvent *e);
static void centerfloater(const Arg *arg);
static void checkotherwm(void);
static void cleanupmon(Monitor *mon);
static void cleanup(void);
static void cleanupfont(FontInfo *fi);
static void clearurgent(Client *c);
static void clientmessage(XEvent *e);
static void configure(Client *c);
static void configurenotify(XEvent *e);
static void configurerequest(XEvent *e);
static void createallbarriers(void);
static Monitor *createmon(void);
static void destroyallbarriers(void);
static void destroynotify(XEvent *e);
static void detach(Client *c);
static void detachstack(Client *c);
static void die(const char *errstr, ...);
static Monitor *dirtomon(int dir);
static void drawbar(Monitor *m);
static void drawbars(void);
static void drawsquare(Bool filled, Bool empty, Bool invert, unsigned long col[ColLast]);
static void drawtext(const char *text, unsigned long col[ColLast], Bool invert);
static void enternotify(XEvent *e);
static void expose(XEvent *e);
static void focus(Client *c);
static void focusin(XEvent *e);
static void focusmon(const Arg *arg);
static void focusmonwarp(const Arg *arg);
static void focusstack(const Arg *arg);
static unsigned long getcolor(const char *colstr);
static Bool getrootptr(int *x, int *y);
static long getstate(Window w);
static Bool gettextprop(Window w, Atom atom, char *text, unsigned int size);
static void grabbuttons(Client *c, Bool focused);
static void grabkeys(void);
static void incnmaster(const Arg *arg);
static void initfont(const char *fontstr, FontInfo *fi);
static void keypress(XEvent *e);
static void killclient(const Arg *arg);
static void manage(Window w, XWindowAttributes *wa);
static void mappingnotify(XEvent *e);
static void maprequest(XEvent *e);
static void maximizefloater(const Arg *arg);
static void modgap(const Arg *a);
static void monocle(Monitor *m);
static void motionnotify(XEvent *e);
static void movemouse(const Arg *arg);
static void movestack(const Arg *arg);
static unsigned long multiplycolor(unsigned long col, double fact);
static Client *nexttiled(Client *c);
static void pop(Client *);
static void propertynotify(XEvent *e);
static void quit(const Arg *arg);
static Monitor *recttomon(int x, int y, int w, int h);
static void resize(Client *c, int x, int y, int w, int h, Bool interact);
static void resizeclient(Client *c, int x, int y, int w, int h);
static void resizemouse(const Arg *arg);
static void restack(Monitor *m);
static void restart(const Arg *arg);
static void run(void);
static void scan(void);
static Bool sendevent(Client *c, Atom proto);
static void sendmon(Client *c, Monitor *m);
static void setborder(Client *c, enum BorderType state);
static void setclientstate(Client *c, long state);
static void setfocus(Client *c);
static void setfullscreen(Client *c, Bool fullscreen);
static void setlayout(const Arg *arg);
static void setmfact(const Arg *arg);
static void setshape(Client *c);
static void setup(void);
static void shiftmask(unsigned int *m, int dir);
static void shiftview(const Arg *arg);
static void showhide(Client *c);
static void sigchld(int unused);
static void slinp(Monitor *);
static void spawn(const Arg *arg);
static void swapfocus();
static void tag(const Arg *arg);
static void tagmon(const Arg *arg);
static void tagrel(const Arg *arg);
static int textnw(const char *text, unsigned int len, FontInfo *fi);
static void tile(Monitor *);
static void togglebar(const Arg *arg);
static void togglefloating(const Arg *arg);
static void togglefullscreen(const Arg *arg);
static void toggletag(const Arg *arg);
static void toggleview(const Arg *arg);
static void unfocus(Client *c, Bool setfocus);
static void unmanage(Client *c, Bool destroyed);
static void unmapnotify(XEvent *e);
static void updatebarpos(Monitor *m);
static void updatebars(void);
static void updateclientlist(void);
static Bool updategeom(void);
static void updatenumlockmask(void);
static void updatesizehints(Client *c);
static void updatestatus(void);
static void updatetitle(Client *c);
static void updatewindowtype(Client *c);
static void updatewmhints(Client *c);
static void view(const Arg *arg);
static Client *wintoclient(Window w);
static Monitor *wintomon(Window w);
static int xerror(Display *dpy, XErrorEvent *ee);
static int xerrordummy(Display *dpy, XErrorEvent *ee);
static int xerrorstart(Display *dpy, XErrorEvent *ee);

/* variables */
static Client *prevclient = NULL;
static const char broken[] = "broken";
static char stext[256];
static int titlepx;
static int totalborderpx;
static int gappx;
static int screen;
static int screenbarriers;
static int sw, sh;           /* X display screen geometry width, height */
static int bh, blw = 0;      /* bar geometry */
static int (*xerrorxlib)(Display *, XErrorEvent *);
static unsigned int numlockmask = 0;
static void (*handler[LASTEvent]) (XEvent *) = {
	[ButtonPress] = buttonpress,
	[ClientMessage] = clientmessage,
	[ConfigureRequest] = configurerequest,
	[ConfigureNotify] = configurenotify,
	[DestroyNotify] = destroynotify,
	[EnterNotify] = enternotify,
	[Expose] = expose,
	[FocusIn] = focusin,
	[KeyPress] = keypress,
	[MappingNotify] = mappingnotify,
	[MapRequest] = maprequest,
	[MotionNotify] = motionnotify,
	[PropertyNotify] = propertynotify,
	[UnmapNotify] = unmapnotify
};
static Atom wmatom[WMLast], netatom[NetLast];
static Bool running = True;
static Bool dorestart = False;
static ColorInfo ci;
static Cursor cursor[CurLast];
static Display *dpy;
static DC dc;
static FontInfo fibar, fititle;
static Monitor *mons = NULL, *selmon = NULL;
static Window root;

/* configuration, allows nested code to access above variables */
#include "config.h"

/* compile-time check if all tags fit into an unsigned int bit array. */
struct NumTags { char limitexceeded[LENGTH(tags) > 31 ? -1 : 1]; };

/* function implementations */
void
applyrules(Client *c) {
	const char *class, *instance;
	unsigned int i;
	const Rule *r;
	Monitor *m;
	XClassHint ch = { NULL, NULL };

	/* rule matching */
	c->isfloating = c->tags = 0;
	c->sizehints = sizehints_default;
	XGetClassHint(dpy, c->win, &ch);
	class    = ch.res_class ? ch.res_class : broken;
	instance = ch.res_name  ? ch.res_name  : broken;

	for(i = 0; i < LENGTH(rules); i++) {
		r = &rules[i];
		if((!r->title || strstr(c->name, r->title))
		&& (!r->class || strstr(class, r->class))
		&& (!r->instance || strstr(instance, r->instance)))
		{
			c->isfloating = r->isfloating;
			c->sizehints = r->sizehints;
			c->tags |= r->tags;
			for(m = mons; m && m->num != r->monitor; m = m->next);
			if(m)
				c->mon = m;
		}
	}
	if(ch.res_class)
		XFree(ch.res_class);
	if(ch.res_name)
		XFree(ch.res_name);
	c->tags = c->tags & TAGMASK ? c->tags & TAGMASK : c->mon->tagset[c->mon->seltags];
}

Bool
applysizehints(Client *c, int *x, int *y, int *w, int *h, Bool interact) {
	Bool baseismin;
	Monitor *m = c->mon;

	/* set minimum possible */
	*w = MAX(1, *w);
	*h = MAX(1, *h);
	if(interact) {
		if(*x > sw)
			*x = sw - WIDTH(c);
		if(*y > sh)
			*y = sh - HEIGHT(c);
		if(*x + *w + (2 * totalborderpx) < 0)
			*x = 0;
		if(*y + *h + (2 * totalborderpx + titlepx) < 0)
			*y = 0;
	}
	else {
		if(*x >= m->wx + m->ww)
			*x = m->wx + m->ww - WIDTH(c);
		if(*y >= m->wy + m->wh)
			*y = m->wy + m->wh - HEIGHT(c);
		if(*x + *w + (2 * totalborderpx) <= m->wx)
			*x = m->wx;
		if(*y + *h + (2 * totalborderpx + titlepx) <= m->wy)
			*y = m->wy;
	}
	if(*h < bh)
		*h = bh;
	if(*w < bh)
		*w = bh;
	if(c->sizehints || c->isfloating || !c->mon->lt->arrange) {
		/* see last two sentences in ICCCM 4.1.2.3 */
		baseismin = c->basew == c->minw && c->baseh == c->minh;
		if(!baseismin) { /* temporarily remove base dimensions */
			*w -= c->basew;
			*h -= c->baseh;
		}
		/* adjust for aspect limits */
		if(c->mina > 0 && c->maxa > 0) {
			if(c->maxa < (float)*w / *h)
				*w = *h * c->maxa + 0.5;
			else if(c->mina < (float)*h / *w)
				*h = *w * c->mina + 0.5;
		}
		if(baseismin) { /* increment calculation requires this */
			*w -= c->basew;
			*h -= c->baseh;
		}
		/* adjust for increment value */
		if(c->incw)
			*w -= *w % c->incw;
		if(c->inch)
			*h -= *h % c->inch;
		/* restore base dimensions */
		*w = MAX(*w + c->basew, c->minw);
		*h = MAX(*h + c->baseh, c->minh);
		if(c->maxw)
			*w = MIN(*w, c->maxw);
		if(c->maxh)
			*h = MIN(*h, c->maxh);
	}
	return *x != c->x || *y != c->y || *w != c->w || *h != c->h;
}

void
arrange(Monitor *m) {
	if(m)
		showhide(m->stack);
	else for(m = mons; m; m = m->next)
		showhide(m->stack);
	if(m) {
		arrangemon(m);
		restack(m);
	} else for(m = mons; m; m = m->next)
		arrangemon(m);
}

void
arrangemon(Monitor *m) {
	strncpy(m->ltsymbol, m->lt->symbol, sizeof m->ltsymbol);
	if(m->lt->arrange)
		m->lt->arrange(m);
}

void
attach(Client *c) {
	c->next = c->mon->clients;
	c->mon->clients = c;
}

void
attachstack(Client *c) {
	c->snext = c->mon->stack;
	c->mon->stack = c;
}

void
buttonpress(XEvent *e) {
	unsigned int i, click;
	Client *c;
	Monitor *m;
	XButtonPressedEvent *ev = &e->xbutton;

	click = ClkRootWin;
	/* focus monitor if necessary */
	if((m = wintomon(ev->window)) && m != selmon) {
		unfocus(selmon->sel, True);
		selmon = m;
		focus(NULL);
	}
	if((c = wintoclient(ev->window))) {
		focus(c);
		click = ClkClientWin;
	}
	for(i = 0; i < LENGTH(buttons); i++)
		if(click == buttons[i].click && buttons[i].func && buttons[i].button == ev->button
		&& CLEANMASK(buttons[i].mask) == CLEANMASK(ev->state))
			buttons[i].func(&buttons[i].arg);
}

void
centerfloater(const Arg *arg) {
	if(!selmon->sel || selmon->sel->isfullscreen ||
	   !(selmon->sel->isfloating || !selmon->lt->arrange))
		return;
	resize(selmon->sel,
			selmon->wx + 0.5 * (selmon->ww - selmon->sel->w - 2*selmon->sel->bw),
			selmon->wy + 0.5 * (selmon->wh - selmon->sel->h - 2*selmon->sel->bw + titlepx),
			selmon->sel->w, selmon->sel->h,
			False);
}

void
checkotherwm(void) {
	xerrorxlib = XSetErrorHandler(xerrorstart);
	/* this causes an error if some other window manager is running */
	XSelectInput(dpy, DefaultRootWindow(dpy), SubstructureRedirectMask);
	XSync(dpy, False);
	XSetErrorHandler(xerror);
	XSync(dpy, False);
}

void
cleanupmon(Monitor *mon) {
	Monitor *m;

	if(mon == mons)
		mons = mons->next;
	else {
		for(m = mons; m && m->next != mon; m = m->next);
		m->next = mon->next;
	}
	XUnmapWindow(dpy, mon->barwin);
	XDestroyWindow(dpy, mon->barwin);
	free(mon);
}

void
cleanup(void) {
	Arg a = {.ui = ~0};
	Layout foo = { "", NULL };
	Monitor *m;

	destroyallbarriers();
	view(&a);
	selmon->lt = &foo;
	for(m = mons; m; m = m->next)
		while(m->stack)
			unmanage(m->stack, False);
	cleanupfont(&fibar);
	cleanupfont(&fititle);
	XUngrabKey(dpy, AnyKey, AnyModifier, root);
	XFreePixmap(dpy, dc.drawable);
	XFreeGC(dpy, dc.gc);
	XFreeCursor(dpy, cursor[CurNormal]);
	XFreeCursor(dpy, cursor[CurResize]);
	XFreeCursor(dpy, cursor[CurMove]);
	while(mons)
		cleanupmon(mons);
	XSync(dpy, False);
	XSetInputFocus(dpy, PointerRoot, RevertToPointerRoot, CurrentTime);
	XDeleteProperty(dpy, root, netatom[NetActiveWindow]);
}

void
cleanupfont(FontInfo *fi) {
	if(fi->set)
		XFreeFontSet(dpy, fi->set);
	else
		XFreeFont(dpy, fi->xfont);
}

void
clearurgent(Client *c) {
	XWMHints *wmh;

	c->isurgent = False;
	if(!(wmh = XGetWMHints(dpy, c->win)))
		return;
	wmh->flags &= ~XUrgencyHint;
	XSetWMHints(dpy, c->win, wmh);
	XFree(wmh);
}

void
clientmessage(XEvent *e) {
	XClientMessageEvent *cme = &e->xclient;
	Client *c = wintoclient(cme->window);

	if(!c)
		return;
	if(cme->message_type == netatom[NetWMState]) {
		if(cme->data.l[1] == netatom[NetWMFullscreen] || cme->data.l[2] == netatom[NetWMFullscreen])
			setfullscreen(c, (cme->data.l[0] == 1 /* _NET_WM_STATE_ADD    */
			              || (cme->data.l[0] == 2 /* _NET_WM_STATE_TOGGLE */ && !c->isfullscreen)));
	}
	else if(cme->message_type == netatom[NetActiveWindow]) {
		if(!ISVISIBLE(c)) {
			c->mon->seltags ^= 1;
			c->mon->tagset[c->mon->seltags] = c->tags;
		}
		pop(c);
	}
}

void
configure(Client *c) {
	XConfigureEvent ce;

	ce.type = ConfigureNotify;
	ce.display = dpy;
	ce.event = c->win;
	ce.window = c->win;
	ce.x = c->x;
	ce.y = c->y;
	ce.width = c->w;
	ce.height = c->h;
	ce.border_width = c->bw;
	ce.above = None;
	ce.override_redirect = False;
	XSendEvent(dpy, c->win, False, StructureNotifyMask, (XEvent *)&ce);
	setborder(c, StateAuto);
	setshape(c);
}

void
configurenotify(XEvent *e) {
	Monitor *m;
	XConfigureEvent *ev = &e->xconfigure;
	Bool dirty;
	Client *c = wintoclient(ev->window);

	// TODO: updategeom handling sucks, needs to be simplified
	if(ev->window == root) {
		dirty = (sw != ev->width || sh != ev->height);
		sw = ev->width;
		sh = ev->height;
		if(updategeom() || dirty) {
			if(dc.drawable != 0)
				XFreePixmap(dpy, dc.drawable);
			dc.drawable = XCreatePixmap(dpy, root, sw, bh, DefaultDepth(dpy, screen));
			updatebars();
			for(m = mons; m; m = m->next)
				XMoveResizeWindow(dpy, m->barwin, m->wx, m->by, m->ww, bh);
			focus(NULL);
			arrange(NULL);
		}
	}
	else if(c) {
		setborder(c, StateAuto);
	}
}

void
configurerequest(XEvent *e) {
	Client *c;
	Monitor *m;
	XConfigureRequestEvent *ev = &e->xconfigurerequest;
	XWindowChanges wc;

	if((c = wintoclient(ev->window))) {
		if(ev->value_mask & CWBorderWidth)
			c->bw = ev->border_width;
		else if(c->isfloating || !selmon->lt->arrange) {
			m = c->mon;
			if(ev->value_mask & CWX) {
				c->oldx = c->x;
				c->x = m->mx + ev->x;
			}
			if(ev->value_mask & CWY) {
				c->oldy = c->y;
				c->y = m->my + ev->y;
			}
			if(ev->value_mask & CWWidth) {
				c->oldw = c->w;
				c->w = ev->width;
				setshape(c);
			}
			if(ev->value_mask & CWHeight) {
				c->oldh = c->h;
				c->h = ev->height;
				setshape(c);
			}
			if((c->x + c->w) > m->mx + m->mw && c->isfloating)
				c->x = m->mx + (m->mw / 2 - WIDTH(c) / 2); /* center in x direction */
			if((c->y + c->h) > m->my + m->mh && c->isfloating)
				c->y = m->my + (m->mh / 2 - HEIGHT(c) / 2); /* center in y direction */
			if((ev->value_mask & (CWX|CWY)) && !(ev->value_mask & (CWWidth|CWHeight)))
				configure(c);
			if(ISVISIBLE(c))
				XMoveResizeWindow(dpy, c->win, c->x, c->y, c->w, c->h);
		}
		else
			configure(c);
	}
	else {
		wc.x = ev->x;
		wc.y = ev->y;
		wc.width = ev->width;
		wc.height = ev->height;
		wc.border_width = ev->border_width;
		wc.sibling = ev->above;
		wc.stack_mode = ev->detail;
		XConfigureWindow(dpy, ev->window, ev->value_mask, &wc);
	}
	XSync(dpy, False);
}

void
createallbarriers(void) {
	Monitor *m;

	if(screenbarriers) {
		for(m = mons; m; m = m->next) {
			if(m->showbar) {
				/* Top, bottom, left, right */
				m->barrier[0] = XFixesCreatePointerBarrier(dpy, root,
						m->wx, m->wy + totalborderpx + titlepx + gappx,
						m->wx + m->ww - 1, m->wy + totalborderpx + titlepx + gappx,
						BarrierPositiveY,
						0, NULL);
				m->barrier[1] = XFixesCreatePointerBarrier(dpy, root,
						m->wx, m->wy + m->wh - totalborderpx - gappx,
						m->wx + m->ww - 1, m->wy + m->wh - totalborderpx - gappx,
						BarrierNegativeY,
						0, NULL);
				m->barrier[2] = XFixesCreatePointerBarrier(dpy, root,
						m->wx + totalborderpx + gappx, m->wy,
						m->wx + totalborderpx + gappx, m->wy + m->wh - 1,
						BarrierPositiveX,
						0, NULL);
				m->barrier[3] = XFixesCreatePointerBarrier(dpy, root,
						m->wx + m->ww - totalborderpx - gappx, m->wy,
						m->wx + m->ww - totalborderpx - gappx, m->wy + m->wh - 1,
						BarrierNegativeX,
						0, NULL);
			}
		}
	}
}

Monitor *
createmon(void) {
	Monitor *m;

	if(!(m = (Monitor *)calloc(1, sizeof(Monitor))))
		die("fatal: could not malloc() %u bytes\n", sizeof(Monitor));
	m->tagset[0] = m->tagset[1] = startuptags;
	m->mfact = mfact;
	m->nmaster = nmaster;
	m->nmaster_dynamic_max = nmaster_dynamic_max;
	m->showbar = showbar;
	m->topbar = topbar;
	m->lt = &layouts[0];
	strncpy(m->ltsymbol, layouts[0].symbol, sizeof m->ltsymbol);
	return m;
}

void
destroyallbarriers(void) {
	Monitor *m;
	int i;

	if(screenbarriers)
		for(m = mons; m; m = m->next)
			if(m->showbar)
				for(i = 0; i < 4; i++)
					XFixesDestroyPointerBarrier(dpy, m->barrier[i]);
}

void
destroynotify(XEvent *e) {
	Client *c;
	XDestroyWindowEvent *ev = &e->xdestroywindow;

	if((c = wintoclient(ev->window)))
		unmanage(c, True);
}

void
detach(Client *c) {
	Client **tc;

	for(tc = &c->mon->clients; *tc && *tc != c; tc = &(*tc)->next);
	*tc = c->next;
}

void
detachstack(Client *c) {
	Client **tc, *t;

	for(tc = &c->mon->stack; *tc && *tc != c; tc = &(*tc)->snext);
	*tc = c->snext;

	if(c == c->mon->sel) {
		for(t = c->mon->stack; t && !ISVISIBLE(t); t = t->snext);
		c->mon->sel = t;
	}
}

void
die(const char *errstr, ...) {
	va_list ap;

	va_start(ap, errstr);
	vfprintf(stderr, errstr, ap);
	va_end(ap);
	exit(EXIT_FAILURE);
}

Monitor *
dirtomon(int dir) {
	Monitor *m = NULL;

	if(dir > 0) {
		if(!(m = selmon->next))
			m = mons;
	}
	else if(selmon == mons)
		for(m = mons; m->next; m = m->next);
	else
		for(m = mons; m->next != selmon; m = m->next);
	return m;
}

void
drawbar(Monitor *m) {
	int x;
	unsigned int i, occ = 0, urg = 0;
	unsigned long *col;
	Client *c;

	XSetForeground(dpy, dc.gc, ci.baremptycolor);
	XFillRectangle(dpy, dc.drawable, dc.gc, 0, 0, m->ww, bh);

	for(c = m->clients; c; c = c->next) {
		occ |= c->tags;
		if(c->isurgent)
			urg |= c->tags;
	}
	dc.x = 0;
	dc.y = m->topbar ? 0 : 1;
	dc.h = bh;
	dc.fi = &fibar;
	for(i = 0; i < LENGTH(tags); i++) {
		if (!((occ | m->tagset[m->seltags]) & 1 << i))
			continue;
		dc.w = TEXTW(tags[i], fibar);
		col = m->tagset[m->seltags] & 1 << i ? ci.infosel : ci.infonorm;
		drawtext(tags[i], col, urg & 1 << i);
		drawsquare(m == selmon && selmon->sel && selmon->sel->tags & 1 << i,
		           occ & 1 << i, urg & 1 << i, col);
		dc.x += dc.w;
	}
	dc.w = blw = TEXTW(m->ltsymbol, fibar);
	drawtext(m->ltsymbol, ci.infonorm, False);
	dc.x += dc.w;
	x = dc.x;

	dc.w = TEXTW(stext, fibar);
	dc.x = m->ww - dc.w;
	if(dc.x < x) {
		dc.x = x;
		dc.w = m->ww - x;
	}
	drawtext(stext, ci.infonorm, False);

	/* Draw border. */
	XSetForeground(dpy, dc.gc, ci.linecolor);
	if (topbar)
		XDrawLine(dpy, dc.drawable, dc.gc, 0, bh - 1, m->ww, bh - 1);
	else
		XDrawLine(dpy, dc.drawable, dc.gc, 0, 0, m->ww, 0);

	XCopyArea(dpy, dc.drawable, m->barwin, dc.gc, 0, 0, m->ww, bh, 0, 0);
	XSync(dpy, False);
}

void
drawbars(void) {
	Monitor *m;

	for(m = mons; m; m = m->next)
		drawbar(m);
}

void
drawsquare(Bool filled, Bool empty, Bool invert, unsigned long col[ColLast]) {
	int x;

	XSetForeground(dpy, dc.gc, col[invert ? ColBG : ColFG]);
	x = (dc.fi->ascent + dc.fi->descent + 2) / 4;
	if(filled)
		XFillRectangle(dpy, dc.drawable, dc.gc, dc.x+1, dc.y+1, x+1, x+1);
	else if(empty)
		XDrawRectangle(dpy, dc.drawable, dc.gc, dc.x+1, dc.y+1, x, x);
}

void
drawtext(const char *text, unsigned long col[ColLast], Bool invert) {
	char buf[256];
	int i, x, y, h, len, olen;

	XSetForeground(dpy, dc.gc, col[invert ? ColFG : ColBG]);
	XFillRectangle(dpy, dc.drawable, dc.gc, dc.x, dc.y, dc.w, dc.h);
	if(!text)
		return;
	olen = strlen(text);
	h = dc.fi->ascent + dc.fi->descent;
	y = dc.y + (dc.h / 2) - (h / 2) + dc.fi->ascent;
	x = dc.x + (h / 2);
	/* shorten text if necessary */
	for(len = MIN(olen, sizeof buf); len && textnw(text, len, dc.fi) > dc.w - h; len--);
	if(!len)
		return;
	memcpy(buf, text, len);
	if(len < olen)
		for(i = len; i && i > len - 3; buf[--i] = '.');
	XSetForeground(dpy, dc.gc, col[invert ? ColBG : ColFG]);
	if(dc.fi->set)
		XmbDrawString(dpy, dc.drawable, dc.fi->set, dc.gc, x, y, buf, len);
	else
		XDrawString(dpy, dc.drawable, dc.gc, x, y, buf, len);
}

void
drawtitletext(const char *text, unsigned long col, GC gc, Drawable d, int w) {
	/* TODO refactor, this is mostly a copy of drawtext() */

	char buf[512];
	int i, x, y, h, len, olen;

	if(!text)
		return;
	olen = strlen(text);
	h = dc.fi->ascent + dc.fi->descent;
	/* shorten text if necessary */
	for(len = MIN(olen, sizeof buf); len && textnw(text, len, dc.fi) > w - h; len--);
	if(!len)
		return;
	memcpy(buf, text, len);
	if(len < olen)
		for(i = len; i && i > len - 3; buf[--i] = '.');
	x = titlepx + totalborderpx + beveltitle + (h / 2)
	    + (centertitle ? ((w - h) / 2) - textnw(text, len, dc.fi) / 2 : 0);
	y = totalborderpx + beveltitle + ((dc.fi->height + 2) / 2) - (h / 2) +
	    dc.fi->ascent;
	XSetForeground(dpy, gc, col);
	if(dc.fi->set)
		XmbDrawString(dpy, d, dc.fi->set, gc, x, y, buf, len);
	else
		XDrawString(dpy, d, gc, x, y, buf, len);
}

void
enternotify(XEvent *e) {
	Client *c;
	Monitor *m;
	XCrossingEvent *ev = &e->xcrossing;

	if((ev->mode != NotifyNormal || ev->detail == NotifyInferior) && ev->window != root)
		return;
	c = wintoclient(ev->window);
	m = c ? c->mon : wintomon(ev->window);
	if(m != selmon) {
		unfocus(selmon->sel, True);
		selmon = m;
	}
	else if(!c || c == selmon->sel)
		return;
	focus(c);
}

void
expose(XEvent *e) {
	Monitor *m;
	XExposeEvent *ev = &e->xexpose;

	if(ev->count == 0 && (m = wintomon(ev->window)))
		drawbar(m);
}

void
focus(Client *c) {
	if(!c || !ISVISIBLE(c))
		for(c = selmon->stack; c && !ISVISIBLE(c); c = c->snext);
	/* was if(selmon->sel) */
	if(selmon->sel && selmon->sel != c)
		unfocus(selmon->sel, False);
	if(c) {
		if(c->mon != selmon)
			selmon = c->mon;
		if(c->isurgent)
			clearurgent(c);
		detachstack(c);
		attachstack(c);
		grabbuttons(c, True);
		setborder(c, StateFocused);
		setfocus(c);
	}
	else {
		XSetInputFocus(dpy, root, RevertToPointerRoot, CurrentTime);
		XDeleteProperty(dpy, root, netatom[NetActiveWindow]);
	}
	selmon->sel = c;
	drawbars();
}

void
focusin(XEvent *e) { /* there are some broken focus acquiring clients */
	XFocusChangeEvent *ev = &e->xfocus;

	if(selmon->sel && ev->window != selmon->sel->win)
		setfocus(selmon->sel);
}

void
focusmon(const Arg *arg) {
	Monitor *m;

	if(!mons->next)
		return;
	if((m = dirtomon(arg->i)) == selmon)
		return;
	unfocus(selmon->sel, False); /* s/True/False/ fixes input focus issues
					in gedit and anjuta */
	selmon = m;
	focus(NULL);
}

void
focusmonwarp(const Arg *arg) {
	if(selmon->sel) {
		selmon->lmx = selmon->sel->x + (int)(0.5 * selmon->sel->w);
		selmon->lmy = selmon->sel->y + (int)(0.5 * selmon->sel->h);
	}
	focusmon(arg);
	XWarpPointer(dpy, None, root, 0, 0, 0, 0, selmon->lmx, selmon->lmy);
}

void
focusstack(const Arg *arg) {
	Client *c = NULL, *i;

	if(!selmon->sel)
		return;
	if(arg->i > 0) {
		for(c = selmon->sel->next; c && !ISVISIBLE(c); c = c->next);
		if(!c)
			for(c = selmon->clients; c && !ISVISIBLE(c); c = c->next);
	}
	else {
		for(i = selmon->clients; i != selmon->sel; i = i->next)
			if(ISVISIBLE(i))
				c = i;
		if(!c)
			for(; i; i = i->next)
				if(ISVISIBLE(i))
					c = i;
	}
	if(c) {
		focus(c);
		restack(selmon);
	}
}

Atom
getatomprop(Client *c, Atom prop) {
	int di;
	unsigned long dl;
	unsigned char *p = NULL;
	Atom da, atom = None;

	if(XGetWindowProperty(dpy, c->win, prop, 0L, sizeof atom, False, XA_ATOM,
	                      &da, &di, &dl, &dl, &p) == Success && p) {
		atom = *(Atom *)p;
		XFree(p);
	}
	return atom;
}

unsigned long
getcolor(const char *colstr) {
	Colormap cmap = DefaultColormap(dpy, screen);
	XColor color;

	if(!XAllocNamedColor(dpy, cmap, colstr, &color, &color))
		die("error, cannot allocate color '%s'\n", colstr);
	return color.pixel;
}

Bool
getrootptr(int *x, int *y) {
	int di;
	unsigned int dui;
	Window dummy;

	return XQueryPointer(dpy, root, &dummy, &dummy, x, y, &di, &di, &dui);
}

long
getstate(Window w) {
	int format;
	long result = -1;
	unsigned char *p = NULL;
	unsigned long n, extra;
	Atom real;

	if(XGetWindowProperty(dpy, w, wmatom[WMState], 0L, 2L, False, wmatom[WMState],
	                      &real, &format, &n, &extra, (unsigned char **)&p) != Success)
		return -1;
	if(n != 0)
		result = *p;
	XFree(p);
	return result;
}

Bool
gettextprop(Window w, Atom atom, char *text, unsigned int size) {
	char **list = NULL;
	int n;
	XTextProperty name;

	if(!text || size == 0)
		return False;
	text[0] = '\0';
	XGetTextProperty(dpy, w, &name, atom);
	if(!name.nitems)
		return False;
	if(name.encoding == XA_STRING)
		strncpy(text, (char *)name.value, size - 1);
	else {
		if(XmbTextPropertyToTextList(dpy, &name, &list, &n) >= Success && n > 0 && *list) {
			strncpy(text, *list, size - 1);
			XFreeStringList(list);
		}
	}
	text[size - 1] = '\0';
	XFree(name.value);
	return True;
}

void
grabbuttons(Client *c, Bool focused) {
	updatenumlockmask();
	{
		unsigned int i, j;
		unsigned int modifiers[] = { 0, LockMask, numlockmask, numlockmask|LockMask };
		XUngrabButton(dpy, AnyButton, AnyModifier, c->win);
		if(focused) {
			for(i = 0; i < LENGTH(buttons); i++)
				if(buttons[i].click == ClkClientWin)
					for(j = 0; j < LENGTH(modifiers); j++)
						XGrabButton(dpy, buttons[i].button,
						            buttons[i].mask | modifiers[j],
						            c->win, False, BUTTONMASK,
						            GrabModeAsync, GrabModeSync, None, None);
		}
		else
			XGrabButton(dpy, AnyButton, AnyModifier, c->win, False,
			            BUTTONMASK, GrabModeAsync, GrabModeSync, None, None);
	}
}

void
grabkeys(void) {
	updatenumlockmask();
	{
		unsigned int i, j;
		unsigned int modifiers[] = { 0, LockMask, numlockmask, numlockmask|LockMask };
		KeyCode code;

		XUngrabKey(dpy, AnyKey, AnyModifier, root);
		for(i = 0; i < LENGTH(keys); i++)
			if((code = XKeysymToKeycode(dpy, keys[i].keysym)))
				for(j = 0; j < LENGTH(modifiers); j++)
					XGrabKey(dpy, code, keys[i].mod | modifiers[j], root,
						 True, GrabModeAsync, GrabModeAsync);
	}
}

void
incnmaster(const Arg *arg) {
	selmon->nmaster = MAX(selmon->nmaster + arg->i, 0);
	arrange(selmon);
}

void
initfont(const char *fontstr, FontInfo *fi) {
	char *def, **missing;
	int n;

	fi->set = XCreateFontSet(dpy, fontstr, &missing, &n, &def);
	if(missing) {
		while(n--)
			fprintf(stderr, "dwm: missing fontset: %s\n", missing[n]);
		XFreeStringList(missing);
	}
	if(fi->set) {
		XFontStruct **xfonts;
		char **font_names;

		fi->ascent = fi->descent = 0;
		XExtentsOfFontSet(fi->set);
		n = XFontsOfFontSet(fi->set, &xfonts, &font_names);
		while(n--) {
			fi->ascent = MAX(fi->ascent, (*xfonts)->ascent);
			fi->descent = MAX(fi->descent,(*xfonts)->descent);
			xfonts++;
		}
	}
	else {
		if(!(fi->xfont = XLoadQueryFont(dpy, fontstr))
		&& !(fi->xfont = XLoadQueryFont(dpy, "fixed")))
			die("error, cannot load font: '%s'\n", fontstr);
		fi->ascent = fi->xfont->ascent;
		fi->descent = fi->xfont->descent;
	}
	fi->height = fi->ascent + fi->descent;
}

static Bool
isuniquegeom(XineramaScreenInfo *unique, size_t n, XineramaScreenInfo *info) {
	while(n--)
		if(unique[n].x_org == info->x_org && unique[n].y_org == info->y_org
		&& unique[n].width == info->width && unique[n].height == info->height)
			return False;
	return True;
}

void
keypress(XEvent *e) {
	unsigned int i;
	KeySym keysym;
	XKeyEvent *ev;

	ev = &e->xkey;
	keysym = XKeycodeToKeysym(dpy, (KeyCode)ev->keycode, 0);
	for(i = 0; i < LENGTH(keys); i++)
		if(keysym == keys[i].keysym
		&& CLEANMASK(keys[i].mod) == CLEANMASK(ev->state)
		&& keys[i].func)
			keys[i].func(&(keys[i].arg));
}

void
killclient(const Arg *arg) {
	if(!selmon->sel)
		return;
	if(!sendevent(selmon->sel, wmatom[WMDelete])) {
		XGrabServer(dpy);
		XSetErrorHandler(xerrordummy);
		XSetCloseDownMode(dpy, DestroyAll);
		XKillClient(dpy, selmon->sel->win);
		XSync(dpy, False);
		XSetErrorHandler(xerror);
		XUngrabServer(dpy);
	}
}

void
manage(Window w, XWindowAttributes *wa) {
	Client *c, *t = NULL;
	Window trans = None;
	XWindowChanges wc;

	if(!(c = calloc(1, sizeof(Client))))
		die("fatal: could not malloc() %u bytes\n", sizeof(Client));
	c->win = w;
	updatetitle(c);
	if(XGetTransientForHint(dpy, w, &trans) && (t = wintoclient(trans))) {
		c->mon = t->mon;
		c->tags = t->tags;
	}
	else {
		c->mon = selmon;
		applyrules(c);
	}
	/* geometry */
	c->x = c->oldx = wa->x;
	c->y = c->oldy = wa->y;
	c->w = c->oldw = wa->width;
	c->h = c->oldh = wa->height;
	c->oldbw = wa->border_width;

	if(c->x + WIDTH(c) > c->mon->mx + c->mon->mw)
		c->x = c->mon->mx + c->mon->mw - WIDTH(c);
	if(c->y + HEIGHT(c) > c->mon->my + c->mon->mh)
		c->y = c->mon->my + c->mon->mh - HEIGHT(c);
	c->x = MAX(c->x, c->mon->mx);
	/* only fix client y-offset, if the client center might cover the bar */
	c->y = MAX(c->y, ((c->mon->by == c->mon->my) && (c->x + (c->w / 2) >= c->mon->wx)
	           && (c->x + (c->w / 2) < c->mon->wx + c->mon->ww)) ? bh : c->mon->my);
	c->bw = totalborderpx + titlepx;

	wc.border_width = c->bw;
	XConfigureWindow(dpy, w, CWBorderWidth, &wc);
	XSetWindowBorder(dpy, w, ci.norm[ColBG]);
	configure(c); /* propagates border_width, if size doesn't change */
	updatewindowtype(c);
	updatesizehints(c);
	updatewmhints(c);
	XSelectInput(dpy, w, EnterWindowMask|FocusChangeMask|PropertyChangeMask|StructureNotifyMask);
	grabbuttons(c, False);
	if(!c->isfloating)
		c->isfloating = c->oldstate = trans != None || c->isfixed;
	if(c->isfloating)
		XRaiseWindow(dpy, c->win);
	attach(c);
	attachstack(c);
	XChangeProperty(dpy, root, netatom[NetClientList], XA_WINDOW, 32, PropModeAppend,
	                (unsigned char *) &(c->win), 1);
	XMoveResizeWindow(dpy, c->win, c->x + 2 * sw, c->y, c->w, c->h); /* some windows require this */
	setclientstate(c, NormalState);
	if (c->mon == selmon)
		unfocus(selmon->sel, False);
	c->mon->sel = c;
	arrange(c->mon);
	XMapWindow(dpy, c->win);
	focus(NULL);
}

void
mappingnotify(XEvent *e) {
	XMappingEvent *ev = &e->xmapping;

	XRefreshKeyboardMapping(ev);
	if(ev->request == MappingKeyboard)
		grabkeys();
}

void
maprequest(XEvent *e) {
	static XWindowAttributes wa;
	XMapRequestEvent *ev = &e->xmaprequest;

	if(!XGetWindowAttributes(dpy, ev->window, &wa))
		return;
	if(wa.override_redirect)
		return;
	if(!wintoclient(ev->window))
		manage(ev->window, &wa);
}

void
maximizefloater(const Arg *arg) {
	if(!selmon->sel || selmon->sel->isfullscreen ||
	   !(selmon->sel->isfloating || !selmon->lt->arrange))
		return;
	resize(selmon->sel,
	       selmon->wx + gappx - titlepx,
	       selmon->wy + gappx,
	       selmon->ww - (2*totalborderpx) - (2*gappx),
	       selmon->wh - (2*totalborderpx + titlepx) - (2*gappx),
	       False);
}

void
modgap(const Arg *a) {
	Monitor *m;

	if(!selmon)
		return;

	destroyallbarriers();
	gappx += a->i;
	fprintf(stderr, "dwm: gap = %d\n", gappx);
	if(gappx < 0)
		gappx = 0;
	for(m = mons; m; m = m->next)
		arrange(m);
	createallbarriers();
}

void
monocle(Monitor *m) {
	unsigned int n = 0;
	Client *c;

	for(c = m->clients; c; c = c->next)
		if(ISVISIBLE(c))
			n++;
	if(n > 0) /* override layout symbol */
		snprintf(m->ltsymbol, sizeof m->ltsymbol, "[%d]", n);
	for(c = nexttiled(m->clients); c; c = nexttiled(c->next))
		resize(c,
		       m->wx + gappx - titlepx,
		       m->wy + gappx,
		       m->ww - (2*totalborderpx) - (2*gappx),
		       m->wh - (2*totalborderpx + titlepx) - (2*gappx),
		       False);
}

void
motionnotify(XEvent *e) {
	static Monitor *mon = NULL;
	Monitor *m;
	XMotionEvent *ev = &e->xmotion;

	if(ev->window != root)
		return;
	if((m = recttomon(ev->x_root, ev->y_root, 1, 1)) != mon && mon) {
		unfocus(selmon->sel, True);
		selmon = m;
		focus(NULL);
	}
	mon = m;
}

void
movemouse(const Arg *arg) {
	int x, y, ocx, ocy, nx, ny;
	Client *c;
	Monitor *m;
	XEvent ev;

	if(!(c = selmon->sel))
		return;
	if(c->isfullscreen) /* no support moving fullscreen windows by mouse */
		return;
	restack(selmon);
	ocx = c->x;
	ocy = c->y;
	if(XGrabPointer(dpy, root, False, MOUSEMASK, GrabModeAsync, GrabModeAsync,
	None, cursor[CurMove], CurrentTime) != GrabSuccess)
		return;
	if(!getrootptr(&x, &y))
		return;
	do {
		XMaskEvent(dpy, MOUSEMASK|ExposureMask|SubstructureRedirectMask, &ev);
		switch(ev.type) {
		case ConfigureRequest:
		case Expose:
		case MapRequest:
			handler[ev.type](&ev);
			break;
		case MotionNotify:
			nx = ocx + (ev.xmotion.x - x);
			ny = ocy + (ev.xmotion.y - y);
			if(nx >= selmon->wx && nx <= selmon->wx + selmon->ww
			&& ny >= selmon->wy && ny <= selmon->wy + selmon->wh) {
				if(abs(selmon->wx - nx) < snap)
					nx = selmon->wx;
				else if(abs((selmon->wx + selmon->ww) - (nx + WIDTH(c))) < snap)
					nx = selmon->wx + selmon->ww - WIDTH(c);
				if(abs(selmon->wy - ny) < snap)
					ny = selmon->wy;
				else if(abs((selmon->wy + selmon->wh) - (ny + HEIGHT(c))) < snap)
					ny = selmon->wy + selmon->wh - HEIGHT(c);
				if(!c->isfloating && selmon->lt->arrange
				&& (abs(nx - c->x) > snap || abs(ny - c->y) > snap))
					togglefloating(NULL);
			}
			if(!selmon->lt->arrange || c->isfloating)
				resize(c, nx, ny, c->w, c->h, True);
			break;
		}
	} while(ev.type != ButtonRelease);
	XUngrabPointer(dpy, CurrentTime);
	if((m = recttomon(c->x, c->y, c->w, c->h)) != selmon) {
		sendmon(c, m);
		selmon = m;
		focus(NULL);
	}
}

void
movestack(const Arg *arg) {
	Client *c = NULL, *p = NULL, *pc = NULL, *i;

	if(!selmon || !selmon->sel)
		return;

	if(arg->i > 0) {
		/* find the client after selmon->sel */
		for(c = selmon->sel->next; c && (!ISVISIBLE(c) || c->isfloating); c = c->next);
		if(!c)
			for(c = selmon->clients; c && (!ISVISIBLE(c) || c->isfloating); c = c->next);

	}
	else {
		/* find the client before selmon->sel */
		for(i = selmon->clients; i != selmon->sel; i = i->next)
			if(ISVISIBLE(i) && !i->isfloating)
				c = i;
		if(!c)
			for(; i; i = i->next)
				if(ISVISIBLE(i) && !i->isfloating)
					c = i;
	}
	/* find the client before selmon->sel and c */
	for(i = selmon->clients; i && (!p || !pc); i = i->next) {
		if(i->next == selmon->sel)
			p = i;
		if(i->next == c)
			pc = i;
	}

	/* swap c and selmon->sel selmon->clients in the selmon->clients list */
	if(c && c != selmon->sel) {
		Client *temp = selmon->sel->next==c?selmon->sel:selmon->sel->next;
		selmon->sel->next = c->next==selmon->sel?c:c->next;
		c->next = temp;

		if(p && p != c)
			p->next = c;
		if(pc && pc != selmon->sel)
			pc->next = selmon->sel;

		if(selmon->sel == selmon->clients)
			selmon->clients = c;
		else if(c == selmon->clients)
			selmon->clients = selmon->sel;

		arrange(selmon);
	}
}

unsigned long
multiplycolor(unsigned long col, double fact) {
	int r = (int)(((0xFF0000 & col) >> 16) * fact);
	int g = (int)(((0x00FF00 & col) >>  8) * fact);
	int b = (int)(((0x0000FF & col) >>  0) * fact);
	r = r > 255 ? 255 : r;
	g = g > 255 ? 255 : g;
	b = b > 255 ? 255 : b;
	return (0xFF000000 & col) | (r << 16) | (g <<  8) | (b <<  0);
}

Client *
nexttiled(Client *c) {
	for(; c && (c->isfloating || !ISVISIBLE(c)); c = c->next);
	return c;
}

void
pop(Client *c) {
	detach(c);
	attach(c);
	focus(c);
	arrange(c->mon);
}

void
propertynotify(XEvent *e) {
	Client *c;
	Window trans;
	XPropertyEvent *ev = &e->xproperty;

	if((ev->window == root) && (ev->atom == XA_WM_NAME))
		updatestatus();
	else if(ev->state == PropertyDelete)
		return; /* ignore */
	else if((c = wintoclient(ev->window))) {
		switch(ev->atom) {
		default: break;
		case XA_WM_TRANSIENT_FOR:
			if(!c->isfloating && (XGetTransientForHint(dpy, c->win, &trans)) &&
			   (c->isfloating = (wintoclient(trans)) != NULL))
				arrange(c->mon);
			break;
		case XA_WM_NORMAL_HINTS:
			updatesizehints(c);
			break;
		case XA_WM_HINTS:
			updatewmhints(c);
			drawbars();
			break;
		}
		if(ev->atom == XA_WM_NAME || ev->atom == netatom[NetWMName]) {
			updatetitle(c);
			setborder(c, StateAuto);
		}
		if(ev->atom == netatom[NetWMWindowType])
			updatewindowtype(c);
	}
}

void
quit(const Arg *arg) {
	running = False;
}

Monitor *
recttomon(int x, int y, int w, int h) {
	Monitor *m, *r = selmon;
	int a, area = 0;

	for(m = mons; m; m = m->next)
		if((a = INTERSECT(x, y, w, h, m)) > area) {
			area = a;
			r = m;
		}
	return r;
}

void
resize(Client *c, int x, int y, int w, int h, Bool interact) {
	if(applysizehints(c, &x, &y, &w, &h, interact))
		resizeclient(c, x, y, w, h);
}

void
resizeclient(Client *c, int x, int y, int w, int h) {
	XWindowChanges wc;

	c->oldx = c->x; c->x = wc.x = x;
	c->oldy = c->y; c->y = wc.y = y;
	c->oldw = c->w; c->w = wc.width = w;
	c->oldh = c->h; c->h = wc.height = h;
	wc.border_width = c->bw;
	XConfigureWindow(dpy, c->win, CWX|CWY|CWWidth|CWHeight|CWBorderWidth, &wc);
	configure(c);
	XSync(dpy, False);
}

void
resizemouse(const Arg *arg) {
	int ocx, ocy;
	int nw, nh;
	Client *c;
	Monitor *m;
	XEvent ev;

	if(!(c = selmon->sel))
		return;
	if(c->isfullscreen) /* no support resizing fullscreen windows by mouse */
		return;
	restack(selmon);
	ocx = c->x;
	ocy = c->y;
	if(XGrabPointer(dpy, root, False, MOUSEMASK, GrabModeAsync, GrabModeAsync,
	                None, cursor[CurResize], CurrentTime) != GrabSuccess)
		return;
	XWarpPointer(dpy, None, c->win, 0, 0, 0, 0, c->w + totalborderpx - 1,
	             c->h + totalborderpx - 1);
	do {
		XMaskEvent(dpy, MOUSEMASK|ExposureMask|SubstructureRedirectMask, &ev);
		switch(ev.type) {
		case ConfigureRequest:
		case Expose:
		case MapRequest:
			handler[ev.type](&ev);
			break;
		case MotionNotify:
			nw = MAX(ev.xmotion.x - ocx - 2 * totalborderpx - titlepx + 1, 1);
			nh = MAX(ev.xmotion.y - ocy - 2 * totalborderpx - titlepx + 1, 1);
			if(c->mon->wx + nw >= selmon->wx && c->mon->wx + nw <= selmon->wx + selmon->ww
			&& c->mon->wy + nh >= selmon->wy && c->mon->wy + nh <= selmon->wy + selmon->wh)
			{
				if(!c->isfloating && selmon->lt->arrange
				&& (abs(nw - c->w) > snap || abs(nh - c->h) > snap))
					togglefloating(NULL);
			}
			if(!selmon->lt->arrange || c->isfloating)
				resize(c, c->x, c->y, nw, nh, True);
			break;
		}
	} while(ev.type != ButtonRelease);
	XWarpPointer(dpy, None, c->win, 0, 0, 0, 0, c->w + totalborderpx - 1,
	             c->h + totalborderpx - 1);
	XUngrabPointer(dpy, CurrentTime);
	while(XCheckMaskEvent(dpy, EnterWindowMask, &ev));
	if((m = recttomon(c->x, c->y, c->w, c->h)) != selmon) {
		sendmon(c, m);
		selmon = m;
		focus(NULL);
	}
}

void
restack(Monitor *m) {
	Client *c;
	XEvent ev;
	XWindowChanges wc;

	drawbar(m);
	if(!m->sel)
		return;
	if(m->sel->isfloating || !m->lt->arrange)
		XRaiseWindow(dpy, m->sel->win);
	if(m->lt->arrange) {
		wc.stack_mode = Below;
		wc.sibling = m->barwin;
		for(c = m->stack; c; c = c->snext)
			if(!c->isfloating && ISVISIBLE(c)) {
				XConfigureWindow(dpy, c->win, CWSibling|CWStackMode, &wc);
				wc.sibling = c->win;
			}
	}
	XSync(dpy, False);
	while(XCheckMaskEvent(dpy, EnterWindowMask, &ev));
}

void
restart(const Arg *arg) {
	dorestart = True;
	running = False;
}

void
run(void) {
	XEvent ev;
	/* main event loop */
	XSync(dpy, False);
	while(running && !XNextEvent(dpy, &ev))
		if(handler[ev.type])
			handler[ev.type](&ev); /* call handler */
}

void
scan(void) {
	unsigned int i, num;
	Window d1, d2, *wins = NULL;
	XWindowAttributes wa;

	if(XQueryTree(dpy, root, &d1, &d2, &wins, &num)) {
		for(i = 0; i < num; i++) {
			if(!XGetWindowAttributes(dpy, wins[i], &wa)
			|| wa.override_redirect || XGetTransientForHint(dpy, wins[i], &d1))
				continue;
			if(wa.map_state == IsViewable || getstate(wins[i]) == IconicState)
				manage(wins[i], &wa);
		}
		for(i = 0; i < num; i++) { /* now the transients */
			if(!XGetWindowAttributes(dpy, wins[i], &wa))
				continue;
			if(XGetTransientForHint(dpy, wins[i], &d1)
			&& (wa.map_state == IsViewable || getstate(wins[i]) == IconicState))
				manage(wins[i], &wa);
		}
		if(wins)
			XFree(wins);
	}
}

void
sendmon(Client *c, Monitor *m) {
	if(c->mon == m)
		return;
	unfocus(c, True);
	detach(c);
	detachstack(c);
	c->mon = m;
	c->tags = m->tagset[m->seltags]; /* assign tags of target monitor */
	attach(c);
	attachstack(c);
	focus(NULL);
	arrange(NULL);
}

void
setborder(Client *c, enum BorderType state) {
	Pixmap unshifted, shifted;
	GC gc;
	unsigned long colbase = 0, colouter = 0, colmiddle = 0, colinner = 0;
	int i, j, io;
	XSegment *segs = NULL;
	XSegment cuts[8];
	size_t segsi, bordersi;

	if(c->bw <= 0)
		return;

	if(state == StateAuto) {
		state = (c->isurgent ? StateUrgent :
		                       (c->mon == selmon && c->mon->sel == c ? StateFocused :
		                                                               StateNormal));
	}

	/* X begins tiling the pixmap at the client's origin -- not at the
	 * border's origin. */

	gc = XCreateGC(dpy, root, 0, NULL);
	unshifted = XCreatePixmap(dpy, root, c->w + 2*c->bw, c->h + 2*c->bw,
	                          DefaultDepth(dpy, screen));
	shifted = XCreatePixmap(dpy, root, c->w + 2*c->bw, c->h + 2*c->bw,
	                        DefaultDepth(dpy, screen));

	/* This only shows up if the SHAPE extension is not available. */
	XSetForeground(dpy, gc, 0);
	XFillRectangle(dpy, unshifted, gc, 0, 0, c->w + 2*c->bw, c->h + 2*c->bw);

	/* Draw unshifted */
	switch(state) {
		case StateNormal: colbase = ci.norm[ColBG]; break;
		case StateFocused: colbase = ci.sel[ColBG]; break;
		case StateUrgent: colbase = ci.urg[ColBG]; break;
		case StateAuto: /* silence compiler warning */ break;
	}

	colouter = multiplycolor(colbase, bevelfacts[0]);
	colmiddle = multiplycolor(colbase, bevelfacts[1]);
	colinner = multiplycolor(colbase, bevelfacts[2]);

	XSetForeground(dpy, gc, colmiddle);
	XFillRectangle(dpy, unshifted, gc, titlepx, 0, c->w + 2*totalborderpx,
	               c->h + 2*totalborderpx + titlepx);

	for(io = 0; io <= 1; io++) {
		bordersi = (io == 0 ? 0 : 2);
		if(borders[bordersi] > 0) {
			segs = calloc(sizeof(XSegment), 2*borders[bordersi]);
			if(!segs)
				die("fatal: could not malloc() for pixmap border\n");

			/* left and top */
			XSetForeground(dpy, gc, (io == 0 ? colouter : colinner));
			for(i = 0, segsi = 0; i < borders[bordersi]; i++, segsi++) {
				j = io == 0 ? i : i + borders[0] + borders[1];
				segs[segsi].x1 = j + titlepx;
				segs[segsi].y1 = j;
				segs[segsi].x2 = j + titlepx;
				segs[segsi].y2 = c->h + 2*c->bw - j - 1 - titlepx;
			}
			for(i = 0; i < borders[bordersi]; i++, segsi++) {
				j = io == 0 ? i : i + borders[0] + borders[1];
				segs[segsi].x1 = j + titlepx;
				segs[segsi].y1 = j;
				segs[segsi].x2 = c->w + 2*c->bw - j - 1 - titlepx;
				segs[segsi].y2 = j;
			}
			XDrawSegments(dpy, unshifted, gc, segs, 2*borders[bordersi]);

			/* bottom and right */
			XSetForeground(dpy, gc, (io == 0 ? colinner : colouter));
			for(i = 0, segsi = 0; i < borders[bordersi]; i++, segsi++) {
				j = io == 0 ? i : i + borders[0] + borders[1];
				segs[segsi].x1 = j + 1 + titlepx;
				segs[segsi].y1 = c->h + 2*c->bw - j - 1 - titlepx;
				segs[segsi].x2 = c->w + 2*c->bw - j - 1 - titlepx;
				segs[segsi].y2 = c->h + 2*c->bw - j - 1 - titlepx;
			}
			for(i = 0; i < borders[bordersi]; i++, segsi++) {
				j = io == 0 ? i : i + borders[0] + borders[1];
				segs[segsi].x1 = c->w + 2*c->bw - j - 1 - titlepx;
				segs[segsi].y1 = j + 1;
				segs[segsi].x2 = c->w + 2*c->bw - j - 1 - titlepx;
				segs[segsi].y2 = c->h + 2*c->bw - j - 1 - titlepx;
			}
			XDrawSegments(dpy, unshifted, gc, segs, 2*borders[bordersi]);

			free(segs);
		}
	}

	if((borders[0] > 0 || borders[2] > 0) &&
	   (c->w > 4*(c->bw + 1) && c->h > 4*(c->bw + 1))) {
		/* Top left, top right, bottom left, bottom right. */
		cuts[0].x1 = cuts[0].x2 = 2*titlepx + totalborderpx;
		cuts[0].y1 = MAX(MIN(1, borders[0] - 1), 0);
		cuts[0].y2 = MAX(totalborderpx - 2, borders[0] + borders[1] - 1);

		cuts[1].x1 = cuts[1].x2 = c->w + totalborderpx;
		cuts[1].y1 = cuts[0].y1;
		cuts[1].y2 = cuts[0].y2;

		cuts[2].x1 = cuts[0].x1;
		cuts[2].x2 = cuts[0].x2;
		cuts[2].y1 = cuts[0].y1 + c->h + c->bw;
		cuts[2].y2 = cuts[0].y2 + c->h + c->bw;

		cuts[3].x1 = cuts[1].x1;
		cuts[3].x2 = cuts[1].x2;
		cuts[3].y1 = cuts[1].y1 + c->h + c->bw;
		cuts[3].y2 = cuts[1].y2 + c->h + c->bw;

		/* Left top, left bottom, right top, right bottom. */
		cuts[4].x1 = cuts[0].y1 + titlepx;
		cuts[4].y1 = cuts[0].x1 - titlepx;
		cuts[4].x2 = cuts[0].y2 + titlepx;
		cuts[4].y2 = cuts[0].x2 - titlepx;

		cuts[5].x1 = cuts[4].x1;
		cuts[5].x2 = cuts[4].x2;
		cuts[5].y1 = cuts[5].y2 = c->h + totalborderpx;

		cuts[6].x1 = cuts[4].x1 + c->w + totalborderpx;
		cuts[6].x2 = cuts[4].x2 + c->w + totalborderpx;
		cuts[6].y1 = cuts[4].y1;
		cuts[6].y2 = cuts[4].y2;

		cuts[7].x1 = cuts[5].x1 + c->w + totalborderpx;
		cuts[7].x2 = cuts[5].x2 + c->w + totalborderpx;
		cuts[7].y1 = cuts[5].y1;
		cuts[7].y2 = cuts[5].y2;

		/* Draw bright segments. */
		XSetForeground(dpy, gc, colouter);
		XDrawSegments(dpy, unshifted, gc, cuts, 8);

		/* Shift them by 1px and draw dark segments. */
		for(segsi = 0; segsi <= 3; segsi++) {
			cuts[segsi].x1--;
			cuts[segsi].x2--;
		}
		for(segsi = 4; segsi <= 7; segsi++) {
			cuts[segsi].y1--;
			cuts[segsi].y2--;
		}
		XSetForeground(dpy, gc, colinner);
		XDrawSegments(dpy, unshifted, gc, cuts, 8);
	}

	/* Draw titlebar */
	if(beveltitle > 0) {
		segs = calloc(sizeof(XSegment), 2*beveltitle);
		if(!segs)
			die("fatal: could not malloc() for pixmap title\n");

		/* left and top */
		XSetForeground(dpy, gc, colouter);
		for(i = 0, segsi = 0; i < beveltitle; i++, segsi++) {
			segs[segsi].x1 = titlepx + totalborderpx + i;
			segs[segsi].y1 = totalborderpx + i;
			segs[segsi].x2 = titlepx + totalborderpx + i;
			segs[segsi].y2 = totalborderpx + titlepx - i - 1;
		}
		for(i = 0; i < beveltitle; i++, segsi++) {
			segs[segsi].x1 = titlepx + totalborderpx + i;
			segs[segsi].y1 = totalborderpx + i;
			segs[segsi].x2 = totalborderpx + titlepx + c->w - i - 1;
			segs[segsi].y2 = totalborderpx + i;
		}
		XDrawSegments(dpy, unshifted, gc, segs, 2*beveltitle);

		/* bottom and right */
		XSetForeground(dpy, gc, colinner);
		for(i = 0, segsi = 0; i < beveltitle; i++, segsi++) {
			segs[segsi].x1 = titlepx + totalborderpx + i;
			segs[segsi].y1 = titlepx + totalborderpx - i - 1;
			segs[segsi].x2 = totalborderpx + titlepx + c->w - i - 1;
			segs[segsi].y2 = titlepx + totalborderpx - i - 1;
		}
		for(i = 0; i < beveltitle; i++, segsi++) {
			segs[segsi].x1 = c->w + titlepx + totalborderpx - i - 1;
			segs[segsi].y1 = totalborderpx + i;
			segs[segsi].x2 = c->w + titlepx + totalborderpx - i - 1;
			segs[segsi].y2 = totalborderpx + titlepx - i - 1;
		}
		XDrawSegments(dpy, unshifted, gc, segs, 2*beveltitle);

		free(segs);
	}

	switch(state) {
		case StateNormal: colbase = ci.norm[ColFG]; break;
		case StateFocused: colbase = ci.sel[ColFG]; break;
		case StateUrgent: colbase = ci.urg[ColFG]; break;
		case StateAuto: /* silence compiler warning */ break;
	}
	dc.fi = &fititle;
	drawtitletext(c->name, colbase, gc, unshifted, c->w - 2*beveltitle);

	if(c->isfloating) {
		i = (fititle.ascent + fititle.descent + 2) / 4;
		XDrawRectangle(dpy, unshifted, gc,
		               titlepx + totalborderpx + beveltitle + 1,
		               totalborderpx + beveltitle + 1, i, i);
	}

	/* Shift
	 *
	 * This is what we have drawn above:
	 *
	 *        +----+-----------------+----+
	 *        | tl |   top border    | tr |
	 *        +----+-----------------+----+
	 *        | le |   Client area,  | ri |
	 *        | ft |    invisible    | gh |
	 *        |    |                 | t  |
	 *        +----+-----------------+----+
	 *        | bl |  bottom border  | br |
	 *        +----+-----------------+----+
	 *
	 * However, X starts tiling our pixmap at the client's origin (top
	 * left corner). This means we have to layout our pixmap like this:
	 *
	 *        +=================+====+----+
	 *        #   Client area,  | ri # le |
	 *        #    invisible    | gh # ft |
	 *        #                 | t  #    |
	 *        +-----------------+----+----+
	 *        #  bottom border  | br # bl |
	 *        +=================+====+----+
	 *        |   top border    | tr | tl |
	 *        +-----------------+----+----+
	 *
	 * The area enclosed in "=" and "#" is directly visible. The
	 * "surplus" parts (like left, bottom left, ...) will be wrapped
	 * around!
	 *
	 * I could have drawn everything at it's final position, but I
	 * wanted my drawing algorithms to be simple and independent of this
	 * weird layout.
	 */
	XCopyArea(dpy, unshifted, shifted, gc,         /* top left corner */
	          0, 0,                                       /* src x, y */
	          c->bw, c->bw,                               /* src w, h */
	          c->w + c->bw, c->h + c->bw);                /* dst x, y */
	XCopyArea(dpy, unshifted, shifted, gc,       /* top and top right */
	          c->bw, 0,                                   /* src x, y */
	          c->w + c->bw, c->bw,                        /* src w, h */
	          0, c->h + c->bw);                           /* dst x, y */
	XCopyArea(dpy, unshifted, shifted, gc,    /* left and bottom left */
	          0, c->bw,                                   /* src x, y */
	          c->bw, c->h + c->bw,                        /* src w, h */
	          c->w + c->bw, 0);                           /* dst x, y */
	XCopyArea(dpy, unshifted, shifted, gc,  /* right and bottom right */
	          c->w + c->bw, c->bw,                        /* src x, y */
	          c->bw, c->h + c->bw,                        /* src w, h */
	          c->w, 0);                                   /* dst x, y */
	XCopyArea(dpy, unshifted, shifted, gc,                  /* bottom */
	          c->bw, c->h + c->bw,                        /* src x, y */
	          c->w, c->bw,                                /* src w, h */
	          0, c->h);                                   /* dst x, y */

	XSetWindowBorderPixmap(dpy, c->win, shifted);

	XFreePixmap(dpy, shifted);
	XFreePixmap(dpy, unshifted);
	XFreeGC(dpy, gc);
}

void
setclientstate(Client *c, long state) {
	long data[] = { state, None };

	XChangeProperty(dpy, c->win, wmatom[WMState], wmatom[WMState], 32,
			PropModeReplace, (unsigned char *)data, 2);
}

Bool
sendevent(Client *c, Atom proto) {
	int n;
	Atom *protocols;
	Bool exists = False;
	XEvent ev;

	if(XGetWMProtocols(dpy, c->win, &protocols, &n)) {
		while(!exists && n--)
			exists = protocols[n] == proto;
		XFree(protocols);
	}
	if(exists) {
		ev.type = ClientMessage;
		ev.xclient.window = c->win;
		ev.xclient.message_type = wmatom[WMProtocols];
		ev.xclient.format = 32;
		ev.xclient.data.l[0] = proto;
		ev.xclient.data.l[1] = CurrentTime;
		XSendEvent(dpy, c->win, False, NoEventMask, &ev);
	}
	return exists;
}

void
setfocus(Client *c) {
	if(!c->neverfocus) {
		XSetInputFocus(dpy, c->win, RevertToPointerRoot, CurrentTime);
		XChangeProperty(dpy, root, netatom[NetActiveWindow],
 		                XA_WINDOW, 32, PropModeReplace,
 		                (unsigned char *) &(c->win), 1);
	}
	sendevent(c, wmatom[WMTakeFocus]);
}

void
setfullscreen(Client *c, Bool fullscreen) {
	if(fullscreen) {
		XChangeProperty(dpy, c->win, netatom[NetWMState], XA_ATOM, 32,
		                PropModeReplace, (unsigned char*)&netatom[NetWMFullscreen], 1);
		c->isfullscreen = True;
		c->oldstate = c->isfloating;
		c->oldbw = c->bw;
		c->bw = 0;
		c->isfloating = True;
		resizeclient(c, c->mon->mx, c->mon->my, c->mon->mw, c->mon->mh);
		XRaiseWindow(dpy, c->win);
	}
	else {
		XChangeProperty(dpy, c->win, netatom[NetWMState], XA_ATOM, 32,
		                PropModeReplace, (unsigned char*)0, 0);
		c->isfullscreen = False;
		c->isfloating = c->oldstate;
		c->bw = c->oldbw;
		c->x = c->oldx;
		c->y = c->oldy;
		c->w = c->oldw;
		c->h = c->oldh;
		resizeclient(c, c->x, c->y, c->w, c->h);
		arrange(c->mon);
	}
}

void
togglefullscreen(const Arg *arg) {
	if(!selmon)
		return;

	Client *c = selmon->sel;

	if(!c)
		return;
	if(!c->isfullscreen) {
		setfullscreen(c, True);
	}
	else {
		setfullscreen(c, False);
	}
}

void
setlayout(const Arg *arg) {
	if(arg && arg->v)
		selmon->lt = (Layout *)arg->v;
	strncpy(selmon->ltsymbol, selmon->lt->symbol, sizeof selmon->ltsymbol);
	if(selmon->sel)
		arrange(selmon);
	else
		drawbar(selmon);
}

/* arg > 1.0 will set mfact absolutly */
void
setmfact(const Arg *arg) {
	float f;

	if(!arg || !selmon->lt->arrange)
		return;
	f = arg->f < 1.0 ? arg->f + selmon->mfact : arg->f - 1.0;
	if(f < 0.1 || f > 0.9)
		return;
	selmon->mfact = f;
	arrange(selmon);
}

void
setshape(Client *c) {
	XRectangle r;

	r.x = -totalborderpx;
	r.y = -(totalborderpx + titlepx);
	r.width = c->w + 2*totalborderpx;
	r.height = c->h + 2*totalborderpx + titlepx;
	XShapeCombineRectangles(dpy, c->win, ShapeBounding, 0, 0,
	                        &r, 1, ShapeSet, Unsorted);
}

void
setup(void) {
	XSetWindowAttributes wa;
	int dummy1, dummy2, dummy3;

	/* clean up any zombies immediately */
	sigchld(0);

	/* init const variables from config.h */
	gappx = uselessgap;
	screenbarriers = barriers;
	totalborderpx = borders[0] + borders[1] + borders[2];

	/* init screen */
	screen = DefaultScreen(dpy);
	root = RootWindow(dpy, screen);
	initfont(fontbar, &fibar);
	initfont(fonttitle, &fititle);
	sw = DisplayWidth(dpy, screen);
	sh = DisplayHeight(dpy, screen);
	bh = fibar.height + 3;
	titlepx = fititle.height + 2 + 2*beveltitle;
	if(!XQueryExtension(dpy, "XFIXES", &dummy1, &dummy2, &dummy3)) {
		fprintf(stderr, "dwm: No XFIXES extension available,"
		                "disabling pointer barriers.\n");
		screenbarriers = False;
	}
	if(!XShapeQueryExtension(dpy, &dummy1, &dummy2)) {
		fprintf(stderr, "dwm: No SHAPE extension available, I'll look crappy.\n");
	}
	updategeom();
	/* init atoms */
	wmatom[WMProtocols] = XInternAtom(dpy, "WM_PROTOCOLS", False);
	wmatom[WMDelete] = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
	wmatom[WMState] = XInternAtom(dpy, "WM_STATE", False);
	wmatom[WMTakeFocus] = XInternAtom(dpy, "WM_TAKE_FOCUS", False);
	netatom[NetActiveWindow] = XInternAtom(dpy, "_NET_ACTIVE_WINDOW", False);
	netatom[NetSupported] = XInternAtom(dpy, "_NET_SUPPORTED", False);
	netatom[NetWMName] = XInternAtom(dpy, "_NET_WM_NAME", False);
	netatom[NetWMState] = XInternAtom(dpy, "_NET_WM_STATE", False);
	netatom[NetWMFullscreen] = XInternAtom(dpy, "_NET_WM_STATE_FULLSCREEN", False);
	netatom[NetWMWindowType] = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE", False);
	netatom[NetWMWindowTypeDialog] = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_DIALOG", False);
	netatom[NetClientList] = XInternAtom(dpy, "_NET_CLIENT_LIST", False);
	/* init cursors */
	cursor[CurNormal] = XCreateFontCursor(dpy, XC_left_ptr);
	cursor[CurResize] = XCreateFontCursor(dpy, XC_sizing);
	cursor[CurMove] = XCreateFontCursor(dpy, XC_fleur);
	/* init appearance */
	ci.norm[ColBG] = getcolor(normbgcolor);
	ci.norm[ColFG] = getcolor(normfgcolor);
	ci.sel[ColBG] = getcolor(selbgcolor);
	ci.sel[ColFG] = getcolor(selfgcolor);
	ci.urg[ColBG] = getcolor(urgbgcolor);
	ci.urg[ColFG] = getcolor(urgfgcolor);
	ci.infonorm[ColBG] = getcolor(infonormbgcolor);
	ci.infonorm[ColFG] = getcolor(infonormfgcolor);
	ci.infosel[ColBG] = getcolor(infoselbgcolor);
	ci.infosel[ColFG] = getcolor(infoselfgcolor);
	ci.linecolor = getcolor(linecolor);
	ci.baremptycolor = getcolor(baremptycolor);
	dc.drawable = XCreatePixmap(dpy, root, DisplayWidth(dpy, screen), bh, DefaultDepth(dpy, screen));
	dc.gc = XCreateGC(dpy, root, 0, NULL);
	XSetLineAttributes(dpy, dc.gc, 1, LineSolid, CapButt, JoinMiter);
	/* Applies to fallback for the bar and title bars. */
	if(!fibar.set)
		XSetFont(dpy, dc.gc, fibar.xfont->fid);
	/* init bars */
	updatebars();
	updatestatus();
	/* EWMH support per view */
	XChangeProperty(dpy, root, netatom[NetSupported], XA_ATOM, 32,
			PropModeReplace, (unsigned char *) netatom, NetLast);
	XDeleteProperty(dpy, root, netatom[NetClientList]);
	/* select for events */
	wa.cursor = cursor[CurNormal];
	wa.event_mask = SubstructureRedirectMask|SubstructureNotifyMask|ButtonPressMask|PointerMotionMask
	                |EnterWindowMask|LeaveWindowMask|StructureNotifyMask|PropertyChangeMask;
	XChangeWindowAttributes(dpy, root, CWEventMask|CWCursor, &wa);
	XSelectInput(dpy, root, wa.event_mask);
	grabkeys();
	focus(NULL);
}

void
shiftmask(unsigned int *m, int dir) {
	if(!m)
		return;

	if(dir > 0) /* left circular shift */
		*m = (*m << dir) | (*m >> (LENGTH(tags) - dir));
	else /* right circular shift */
		*m = (*m >> (- dir)) | (*m << (LENGTH(tags) + dir));
}

void
shiftview(const Arg *arg) {
	Arg shifted;
	shifted.ui = selmon->tagset[selmon->seltags];
	shiftmask(&shifted.ui, arg->i);
	view(&shifted);
}

void
showhide(Client *c) {
	if(!c)
		return;
	if(ISVISIBLE(c)) { /* show clients top down */
		XMoveWindow(dpy, c->win, c->x, c->y);
		if((!c->mon->lt->arrange || c->isfloating) && !c->isfullscreen)
			resize(c, c->x, c->y, c->w, c->h, False);
		showhide(c->snext);
	}
	else { /* hide clients bottom up */
		showhide(c->snext);
		XMoveWindow(dpy, c->win, WIDTH(c) * -2, c->y);
	}
}

void
sigchld(int unused) {
	if(signal(SIGCHLD, sigchld) == SIG_ERR)
		die("Can't install SIGCHLD handler");
	while(0 < waitpid(-1, NULL, WNOHANG));
}

void
slinp(Monitor *m) {
	Client *c;
	XClassHint ch = { NULL, NULL };
	const char *class, *instance;
	char *cmin, *cmax, *cthis;
	int imin, imax, ithis;
	int i, slaves = 0, slots, thisslot;
	float xoffrel;

	for(c = nexttiled(m->clients); c; c = nexttiled(c->next)) {
		XGetClassHint(dpy, c->win, &ch);
		class    = ch.res_class ? ch.res_class : broken;
		instance = ch.res_name  ? ch.res_name  : broken;

		if(strcmp(class, "Showpdf") == 0) {
			if(strcmp(instance, "projector") == 0) {
				c->isfloating = 1;
			}
			else {
				cmin = strdup(instance);
				cmax = strchr(cmin, '_');
				cthis = strrchr(cmin, '_');
				*cmax = 0;
				*cthis = 0;
				cmax++;
				cthis++;
				imin = atoi(cmin);
				imax = atoi(cmax);
				ithis = atoi(cthis);
				free(cmin);

				slots = imax - imin + 1;
				thisslot = ithis - imin;

				xoffrel = thisslot / (float)slots;

				resize(c,
				       m->wx + xoffrel * m->ww - titlepx,
				       m->wy,
				       1.0 / slots * m->ww - 2 * totalborderpx,
				       selmon->mfact * m->wh - 2 * totalborderpx - titlepx,
				       False);
			}
		}
		else
			slaves++;

		if(ch.res_class)
			XFree(ch.res_class);
		if(ch.res_name)
			XFree(ch.res_name);
		ch.res_class = NULL;
		ch.res_name = NULL;
	}

	i = 0;
	for(c = nexttiled(m->clients); c; c = nexttiled(c->next)) {
		XGetClassHint(dpy, c->win, &ch);
		class = ch.res_class ? ch.res_class : broken;

		if(strcmp(class, "Showpdf") != 0)
		{
			resize(c,
			       m->wx + (i / (float)slaves) * m->ww - titlepx,
			       m->wy + selmon->mfact * m->wh,
			       1.0 / slaves * m->ww - 2 * totalborderpx,
			       m->wh - selmon->mfact * m->wh - 2 * totalborderpx - titlepx,
			       False);
			i++;
		}

		if(ch.res_class)
			XFree(ch.res_class);
		if(ch.res_name)
			XFree(ch.res_name);
		ch.res_class = NULL;
		ch.res_name = NULL;
	}
}

void
spawn(const Arg *arg) {
	if(fork() == 0) {
		if (fork() == 0) {
			if(dpy)
				close(ConnectionNumber(dpy));
			setsid();
			execvp(((char **)arg->v)[0], (char **)arg->v);
			fprintf(stderr, "dwm: execvp %s", ((char **)arg->v)[0]);
			perror(" failed");
			exit(EXIT_SUCCESS);
		}
		else
			exit(EXIT_SUCCESS);
	}
}

void
swapfocus(){
	Client *c;
	for(c = selmon->clients; c && c != prevclient; c = c->next) ;
	if(c == prevclient) {
		focus(prevclient);
		restack(c->mon);
	}
}

void
tag(const Arg *arg) {
	if(selmon->sel && arg->ui & TAGMASK) {
		selmon->sel->tags = arg->ui & TAGMASK;
		focus(NULL);
		arrange(selmon);
	}
}

void
tagmon(const Arg *arg) {
	if(!selmon->sel || !mons->next)
		return;
	sendmon(selmon->sel, dirtomon(arg->i));
}

void
tagrel(const Arg *arg) {
	Arg shifted;
	if(!selmon->sel)
		return;
	shifted.ui = selmon->sel->tags;
	shiftmask(&shifted.ui, arg->i);
	tag(&shifted);
}

int
textnw(const char *text, unsigned int len, FontInfo *fi) {
	XRectangle r;

	if(fi->set) {
		XmbTextExtents(fi->set, text, len, NULL, &r);
		return r.width;
	}
	return XTextWidth(fi->xfont, text, len);
}

void
tile(Monitor *m) {
	unsigned int i, n, h, mw, my, ty, actual_nmaster;
	Client *c;

	for(n = 0, c = nexttiled(m->clients); c; c = nexttiled(c->next), n++);
	if(n == 0)
		return;

	if (m->nmaster != 0)
		actual_nmaster = m->nmaster;
	else
		actual_nmaster = MIN(MAX(n / 2, 1), m->nmaster_dynamic_max);

	if(n > actual_nmaster)
		mw = actual_nmaster ? m->ww * m->mfact : 0;
	else
		mw = m->ww;
	for(i = my = ty = 0, c = nexttiled(m->clients); c; c = nexttiled(c->next), i++)
		if(i < actual_nmaster) {
			h = (m->wh - my) / (MIN(n, actual_nmaster) - i);
			resize(c,
			       m->wx + gappx - titlepx,
			       m->wy + my + gappx,
			       mw - (2*totalborderpx) - (2*gappx),
			       h - (2*totalborderpx + titlepx) - (2*gappx),
			       False);
			my += HEIGHT(c) + (2*gappx);
		}
		else {
			h = (m->wh - ty) / (n - i);
			resize(c,
			       m->wx + mw + gappx - titlepx,
			       m->wy + ty + gappx,
			       m->ww - mw - (2*totalborderpx) - (2*gappx),
			       h - (2*totalborderpx + titlepx) - (2*gappx),
			       False);
			ty += HEIGHT(c) + (2*gappx);
		}
}

void
togglebar(const Arg *arg) {
	destroyallbarriers();
	selmon->showbar = !selmon->showbar;
	updatebarpos(selmon);
	XMoveResizeWindow(dpy, selmon->barwin, selmon->wx, selmon->by, selmon->ww, bh);
	arrange(selmon);
	createallbarriers();
}

void
togglefloating(const Arg *arg) {
	if(!selmon->sel)
		return;
	if(selmon->sel->isfullscreen) /* no support for fullscreen windows */
		return;
	selmon->sel->isfloating = !selmon->sel->isfloating || selmon->sel->isfixed;
	if(selmon->sel->isfloating)
		resize(selmon->sel, selmon->sel->x, selmon->sel->y,
		       selmon->sel->w, selmon->sel->h, False);
	arrange(selmon);
}

void
toggletag(const Arg *arg) {
	unsigned int newtags;

	if(!selmon->sel)
		return;
	newtags = selmon->sel->tags ^ (arg->ui & TAGMASK);
	if(newtags) {
		selmon->sel->tags = newtags;
		focus(NULL);
		arrange(selmon);
	}
}

void
toggleview(const Arg *arg) {
	unsigned int newtagset = selmon->tagset[selmon->seltags] ^ (arg->ui & TAGMASK);

	if(newtagset) {
		selmon->tagset[selmon->seltags] = newtagset;
		focus(NULL);
		arrange(selmon);
	}
}

void
unfocus(Client *c, Bool setfocus) {
	if(!c)
		return;
	prevclient = c;
	grabbuttons(c, False);
	setborder(c, StateNormal);
	if(setfocus) {
		XSetInputFocus(dpy, root, RevertToPointerRoot, CurrentTime);
		XDeleteProperty(dpy, root, netatom[NetActiveWindow]);
	}
}

void
unmanage(Client *c, Bool destroyed) {
	Monitor *m = c->mon;
	XWindowChanges wc;

	/* The server grab construct avoids race conditions. */
	detach(c);
	detachstack(c);
	if(!destroyed) {
		wc.border_width = c->oldbw;
		XGrabServer(dpy);
		XSetErrorHandler(xerrordummy);
		XConfigureWindow(dpy, c->win, CWBorderWidth, &wc); /* restore border */
		XUngrabButton(dpy, AnyButton, AnyModifier, c->win);
		setclientstate(c, WithdrawnState);
		XSync(dpy, False);
		XSetErrorHandler(xerror);
		XUngrabServer(dpy);
	}
	free(c);
	focus(NULL);
	updateclientlist();
	arrange(m);
}

void
unmapnotify(XEvent *e) {
	Client *c;
	XUnmapEvent *ev = &e->xunmap;

	if((c = wintoclient(ev->window))) {
		if(ev->send_event)
			setclientstate(c, WithdrawnState);
		else
			unmanage(c, False);
	}
}

void
updatebars(void) {
	Monitor *m;
	XSetWindowAttributes wa = {
		.override_redirect = True,
		.background_pixmap = ParentRelative,
		.event_mask = ButtonPressMask|ExposureMask
	};
	for(m = mons; m; m = m->next) {
		if (m->barwin)
			continue;
		m->barwin = XCreateWindow(dpy, root, m->wx, m->by, m->ww, bh, 0, DefaultDepth(dpy, screen),
		                          CopyFromParent, DefaultVisual(dpy, screen),
		                          CWOverrideRedirect|CWBackPixmap|CWEventMask, &wa);
		XDefineCursor(dpy, m->barwin, cursor[CurNormal]);
		XMapRaised(dpy, m->barwin);
	}
}

void
updatebarpos(Monitor *m) {
	m->wy = m->my;
	m->wh = m->mh;
	if(m->showbar) {
		m->wh -= bh;
		m->by = m->topbar ? m->wy : m->wy + m->wh;
		m->wy = m->topbar ? m->wy + bh : m->wy;
	}
	else
		m->by = -bh;
}

void
updateclientlist() {
	Client *c;
	Monitor *m;

	XDeleteProperty(dpy, root, netatom[NetClientList]);
	for(m = mons; m; m = m->next)
		for(c = m->clients; c; c = c->next)
			XChangeProperty(dpy, root, netatom[NetClientList],
			                XA_WINDOW, 32, PropModeAppend,
			                (unsigned char *) &(c->win), 1);
}

Bool
updategeom(void) {
	Bool dirty = False;

	destroyallbarriers();

	if(XineramaIsActive(dpy)) {
		int i, j, n, nn;
		Client *c;
		Monitor *m;
		XineramaScreenInfo *info = XineramaQueryScreens(dpy, &nn);
		XineramaScreenInfo *unique = NULL;

		for(n = 0, m = mons; m; m = m->next, n++);
		/* only consider unique geometries as separate screens */
		if(!(unique = (XineramaScreenInfo *)malloc(sizeof(XineramaScreenInfo) * nn)))
			die("fatal: could not malloc() %u bytes\n", sizeof(XineramaScreenInfo) * nn);
		for(i = 0, j = 0; i < nn; i++)
			if(isuniquegeom(unique, j, &info[i]))
				memcpy(&unique[j++], &info[i], sizeof(XineramaScreenInfo));
		XFree(info);
		nn = j;
		if(n <= nn) {
			for(i = 0; i < (nn - n); i++) { /* new monitors available */
				for(m = mons; m && m->next; m = m->next);
				if(m)
					m->next = createmon();
				else
					mons = createmon();
			}
			for(i = 0, m = mons; i < nn && m; m = m->next, i++)
				if(i >= n
				|| (unique[i].x_org != m->mx || unique[i].y_org != m->my
				    || unique[i].width != m->mw || unique[i].height != m->mh))
				{
					dirty = True;
					m->num = i;
					m->mx = m->wx = unique[i].x_org;
					m->my = m->wy = unique[i].y_org;
					m->mw = m->ww = unique[i].width;
					m->mh = m->wh = unique[i].height;
					m->lmx = m->wx + (int)(0.5 * m->ww);
					m->lmy = m->wy + (int)(0.5 * m->wh);
					updatebarpos(m);
				}
		}
		else { /* less monitors available nn < n */
			for(i = nn; i < n; i++) {
				for(m = mons; m && m->next; m = m->next);
				while(m->clients) {
					dirty = True;
					c = m->clients;
					m->clients = c->next;
					detachstack(c);
					c->mon = mons;
					attach(c);
					attachstack(c);
				}
				if(m == selmon)
					selmon = mons;
				cleanupmon(m);
			}
		}
		free(unique);
	}
	else
	{
		if(!mons)
			mons = createmon();
		if(mons->mw != sw || mons->mh != sh) {
			dirty = True;
			mons->mw = mons->ww = sw;
			mons->mh = mons->wh = sh;
			updatebarpos(mons);
		}
	}
	if(dirty) {
		selmon = mons;
		selmon = wintomon(root);
	}

	createallbarriers();

	return dirty;
}

void
updatenumlockmask(void) {
	unsigned int i, j;
	XModifierKeymap *modmap;

	numlockmask = 0;
	modmap = XGetModifierMapping(dpy);
	for(i = 0; i < 8; i++)
		for(j = 0; j < modmap->max_keypermod; j++)
			if(modmap->modifiermap[i * modmap->max_keypermod + j]
			   == XKeysymToKeycode(dpy, XK_Num_Lock))
				numlockmask = (1 << i);
	XFreeModifiermap(modmap);
}

void
updatesizehints(Client *c) {
	long msize;
	XSizeHints size;

	if(!XGetWMNormalHints(dpy, c->win, &size, &msize))
		/* size is uninitialized, ensure that size.flags aren't used */
		size.flags = PSize;
	if(size.flags & PBaseSize) {
		c->basew = size.base_width;
		c->baseh = size.base_height;
	}
	else if(size.flags & PMinSize) {
		c->basew = size.min_width;
		c->baseh = size.min_height;
	}
	else
		c->basew = c->baseh = 0;
	if(size.flags & PResizeInc) {
		c->incw = size.width_inc;
		c->inch = size.height_inc;
	}
	else
		c->incw = c->inch = 0;
	if(size.flags & PMaxSize) {
		c->maxw = size.max_width;
		c->maxh = size.max_height;
	}
	else
		c->maxw = c->maxh = 0;
	if(size.flags & PMinSize) {
		c->minw = size.min_width;
		c->minh = size.min_height;
	}
	else if(size.flags & PBaseSize) {
		c->minw = size.base_width;
		c->minh = size.base_height;
	}
	else
		c->minw = c->minh = 0;
	if(size.flags & PAspect) {
		c->mina = (float)size.min_aspect.y / size.min_aspect.x;
		c->maxa = (float)size.max_aspect.x / size.max_aspect.y;
	}
	else
		c->maxa = c->mina = 0.0;
	c->isfixed = (c->maxw && c->minw && c->maxh && c->minh
	             && c->maxw == c->minw && c->maxh == c->minh);
}

void
updatetitle(Client *c) {
	if(!gettextprop(c->win, netatom[NetWMName], c->name, sizeof c->name))
		gettextprop(c->win, XA_WM_NAME, c->name, sizeof c->name);
	if(c->name[0] == '\0') /* hack to mark broken clients */
		strcpy(c->name, broken);
}

void
updatewindowtype(Client *c) {
	Atom state = getatomprop(c, netatom[NetWMState]);
	Atom wtype = getatomprop(c, netatom[NetWMWindowType]);

	if(state == netatom[NetWMFullscreen])
		setfullscreen(c, True);
	if(wtype == netatom[NetWMWindowTypeDialog])
		c->isfloating = True;
}

void
updatestatus(void) {
	Monitor *m;
	if(!gettextprop(root, XA_WM_NAME, stext, sizeof(stext)))
		strcpy(stext, "dwm-"VERSION);
	for(m = mons; m; m = m->next)
		drawbar(m);
}

void
updatewmhints(Client *c) {
	XWMHints *wmh;

	if((wmh = XGetWMHints(dpy, c->win))) {
		if(c == selmon->sel && wmh->flags & XUrgencyHint) {
			wmh->flags &= ~XUrgencyHint;
			XSetWMHints(dpy, c->win, wmh);
		}
		else {
			c->isurgent = (wmh->flags & XUrgencyHint) ? True : False;
			if (c->isurgent)
				setborder(c, StateUrgent);
		}
		if(wmh->flags & InputHint)
			c->neverfocus = !wmh->input;
		else
			c->neverfocus = False;
		XFree(wmh);
	}
}

void
view(const Arg *arg) {
	if((arg->ui & TAGMASK) == selmon->tagset[selmon->seltags])
		return;
	selmon->seltags ^= 1; /* toggle sel tagset */
	if(arg->ui & TAGMASK)
		selmon->tagset[selmon->seltags] = arg->ui & TAGMASK;
	focus(NULL);
	arrange(selmon);
}

Client *
wintoclient(Window w) {
	Client *c;
	Monitor *m;

	for(m = mons; m; m = m->next)
		for(c = m->clients; c; c = c->next)
			if(c->win == w)
				return c;
	return NULL;
}

Monitor *
wintomon(Window w) {
	int x, y;
	Client *c;
	Monitor *m;

	if(w == root && getrootptr(&x, &y))
		return recttomon(x, y, 1, 1);
	for(m = mons; m; m = m->next)
		if(w == m->barwin)
			return m;
	if((c = wintoclient(w)))
		return c->mon;
	return selmon;
}

/* There's no way to check accesses to destroyed windows, thus those cases are
 * ignored (especially on UnmapNotify's).  Other types of errors call Xlibs
 * default error handler, which may call exit.  */
int
xerror(Display *dpy, XErrorEvent *ee) {
	if(ee->error_code == BadWindow
	|| (ee->request_code == X_SetInputFocus && ee->error_code == BadMatch)
	|| (ee->request_code == X_PolyText8 && ee->error_code == BadDrawable)
	|| (ee->request_code == X_PolyFillRectangle && ee->error_code == BadDrawable)
	|| (ee->request_code == X_PolySegment && ee->error_code == BadDrawable)
	|| (ee->request_code == X_ChangeWindowAttributes && ee->error_code == BadMatch)
	|| (ee->request_code == X_ConfigureWindow && ee->error_code == BadMatch)
	|| (ee->request_code == X_GrabButton && ee->error_code == BadAccess)
	|| (ee->request_code == X_GrabKey && ee->error_code == BadAccess)
	|| (ee->request_code == X_CopyArea && ee->error_code == BadDrawable))
		return 0;
	fprintf(stderr, "dwm: fatal error: request code=%d, error code=%d\n",
			ee->request_code, ee->error_code);
	return xerrorxlib(dpy, ee); /* may call exit */
}

int
xerrordummy(Display *dpy, XErrorEvent *ee) {
	return 0;
}

/* Startup Error handler to check if another window manager
 * is already running. */
int
xerrorstart(Display *dpy, XErrorEvent *ee) {
	die("dwm: another window manager is already running\n");
	return -1;
}

int
main(int argc, char *argv[]) {
	if(argc == 2 && !strcmp("-v", argv[1]))
		die("dwm-"VERSION", © 2006-"YEAR" dwm engineers, see LICENSE for details\n");
	else if(argc != 1)
		die("usage: dwm [-v]\n");
	if(!setlocale(LC_CTYPE, "") || !XSupportsLocale())
		fputs("warning: no locale support\n", stderr);
	if(!(dpy = XOpenDisplay(NULL)))
		die("dwm: cannot open display\n");
	checkotherwm();
	setup();
	scan();
	run();
	cleanup();
	XCloseDisplay(dpy);
	if (dorestart) {
		execvp(argv[0], argv);
	}
	return EXIT_SUCCESS;
}
