/* Copyright ©2007-2010 Kris Maglione <maglione.k at Gmail>
 * See LICENSE file for license details.
 */
#include "dat.h"
#include <limits.h>
#include "fns.h"

Window *ewmhwin;

static void	ewmh_getwinstate(Client*);
static void	ewmh_setstate(Client*, Atom, int);

#define Net(x) ("_NET_" x)
#define	Action(x) Net("WM_ACTION_" x)
#define	State(x) Net("WM_STATE_" x)
#define	Type(x) Net("WM_WINDOW_TYPE_" x)
#define NET(x) xatom(Net(x))
#define	ACTION(x) xatom(Action(x))
#define	STATE(x) xatom(State(x))
#define	TYPE(x) xatom(Type(x))

void
ewmh_init(void) {
	WinAttr wa;
	char myname[] = "wmii";
	long win;

	ewmhwin = createwindow(&scr.root,
		Rect(0, 0, 1, 1), 0 /*depth*/,
		InputOnly, &wa, 0);

	win = ewmhwin->xid;
	changeprop_long(&scr.root, Net("SUPPORTING_WM_CHECK"), "WINDOW", &win, 1);
	changeprop_long(ewmhwin, Net("SUPPORTING_WM_CHECK"), "WINDOW", &win, 1);
	changeprop_string(ewmhwin, Net("WM_NAME"), myname);

	long zz[] = {0, 0};
	changeprop_long(&scr.root, Net("DESKTOP_VIEWPORT"), "CARDINAL",
		zz, 2);

	long supported[] = {
		/* Misc */
		NET("SUPPORTED"),
		/* Root Properties/Messages */
		NET("ACTIVE_WINDOW"),
		NET("CLOSE_WINDOW"),
		NET("CURRENT_DESKTOP"),
		/* Client Properties */
		NET("FRAME_EXTENTS"),
		NET("WM_DESKTOP"),
		NET("WM_FULLSCREEN_MONITORS"),
		NET("WM_NAME"),
		NET("WM_STRUT"),
		NET("WM_STRUT_PARTIAL"),
		/* States */
		NET("WM_STATE"),
		STATE("DEMANDS_ATTENTION"),
		STATE("FULLSCREEN"),
		STATE("SHADED"),
		/* Window Types */
		NET("WM_WINDOW_TYPE"),
		TYPE("DIALOG"),
		TYPE("DOCK"),
		TYPE("NORMAL"),
		TYPE("SPLASH"),
		/* Actions */
		NET("WM_ALLOWED_ACTIONS"),
		ACTION("FULLSCREEN"),
		/* Desktops */
		NET("DESKTOP_NAMES"),
		NET("NUMBER_OF_DESKTOPS"),
		/* Client List */
		NET("CLIENT_LIST"),
		NET("CLIENT_LIST_STACKING"),
	};
	changeprop_long(&scr.root, Net("SUPPORTED"), "ATOM", supported, nelem(supported));
}

void
ewmh_updateclientlist(void) {
	Vector_long vec;
	Client *c;

	vector_linit(&vec);
	for(c=client; c; c=c->next)
		vector_lpush(&vec, c->w.xid);
	changeprop_long(&scr.root, Net("CLIENT_LIST"), "WINDOW", vec.ary, vec.n);
	free(vec.ary);
}

void
ewmh_updatestacking(void) {
	Vector_long vec;
	Frame *f;
	Area *a;
	View *v;
	int s;

	vector_linit(&vec);

	for(v=view; v; v=v->next) {
		foreach_column(v, s, a)
			for(f=a->frame; f; f=f->anext)
				if(f->client->sel == f)
					vector_lpush(&vec, f->client->w.xid);
	}
	for(v=view; v; v=v->next) {
		for(f=v->floating->stack; f; f=f->snext)
			if(!f->snext) break;
		for(; f; f=f->sprev)
			if(f->client->sel == f)
				vector_lpush(&vec, f->client->w.xid);
	}

	changeprop_long(&scr.root, Net("CLIENT_LIST_STACKING"), "WINDOW", vec.ary, vec.n);
	vector_lfree(&vec);
}

void
ewmh_initclient(Client *c) {
	long allowed[] = {
		ACTION("FULLSCREEN"),
	};

	changeprop_long(&c->w, Net("WM_ALLOWED_ACTIONS"), "ATOM",
		allowed, nelem(allowed));
	ewmh_getwintype(c);
	ewmh_getwinstate(c);
	ewmh_getstrut(c);
	ewmh_updateclientlist();
}

void
ewmh_destroyclient(Client *c) {
	Ewmh *e;

	ewmh_updateclientlist();

	e = &c->w.ewmh;
	if(e->timer)
		if(!ixp_unsettimer(&srv, e->timer))
			fprint(2, "Badness: %C: Can't unset timer\n", c);
	free(c->strut);
}

static void
pingtimeout(long id, void *v) {
	Client *c;

	USED(id);
	c = v;
	event("Unresponsive %C\n", c);
	c->w.ewmh.ping = 0;
	c->w.ewmh.timer = 0;
}

void
ewmh_pingclient(Client *c) {
	Ewmh *e;

	if(!(c->proto & ProtoPing))
		return;

	e = &c->w.ewmh;
	if(e->ping)
		return;

	client_message(c, Net("WM_PING"), c->w.xid);
	e->ping = xtime++;
	e->timer = ixp_settimer(&srv, PingTime, pingtimeout, c);
}

int
ewmh_prop(Client *c, Atom a) {
	if(a == NET("WM_WINDOW_TYPE"))
		ewmh_getwintype(c);
	else
	if(a == NET("WM_STRUT_PARTIAL"))
		ewmh_getstrut(c);
	else
		return 0;
	return 1;
}

typedef struct Prop Prop;
struct Prop {
	char*	name;
	long	mask;
	Atom	atom;
};

static long
getmask(Prop *props, ulong *vals, int n) {
	Prop *p;
	long ret;

	if(props[0].atom == 0)
		for(p=props; p->name; p++)
			p->atom = xatom(p->name);

	ret = 0;
	while(n--) {
		Dprint(DEwmh, "\tvals[%d] = \"%A\"\n", n, vals[n]);
		for(p=props; p->name; p++)
			if(p->atom == vals[n]) {
				ret |= p->mask;
				break;
			}
	}
	return ret;
}

static long
getprop_mask(Window *w, char *prop, Prop *props) {
	ulong *vals;
	long n, mask;

	n = getprop_ulong(w, prop, "ATOM",
		0L, &vals, 16);
	mask = getmask(props, vals, n);
	free(vals);
	return mask;
}

void
ewmh_getwintype(Client *c) {
	static Prop props[] = {
		{Type("DESKTOP"), TypeDesktop},
		{Type("DOCK"),    TypeDock},
		{Type("TOOLBAR"), TypeToolbar},
		{Type("MENU"),    TypeMenu},
		{Type("UTILITY"), TypeUtility},
		{Type("SPLASH"),  TypeSplash},
		{Type("DIALOG"),  TypeDialog},
		{Type("NORMAL"),  TypeNormal},
		{0, }
	};
	long mask;

	mask = getprop_mask(&c->w, Net("WM_WINDOW_TYPE"), props);

	c->w.ewmh.type = mask;
	if(mask & TypeDock) {
		c->borderless = 1;
		c->titleless = 1;
	}
}

static void
ewmh_getwinstate(Client *c) {
	ulong *vals;
	long n;

	n = getprop_ulong(&c->w, Net("WM_STATE"), "ATOM",
		0L, &vals, 16);
	while(--n >= 0)
		ewmh_setstate(c, vals[n], On);
	free(vals);
}

long
ewmh_protocols(Window *w) {
	static Prop props[] = {
		{"WM_DELETE_WINDOW", ProtoDelete},
		{"WM_TAKE_FOCUS", ProtoTakeFocus},
		{Net("WM_PING"), ProtoPing},
		{0, }
	};

	return getprop_mask(w, "WM_PROTOCOLS", props);
}

void
ewmh_getstrut(Client *c) {
	enum {
		Left, Right, Top, Bottom,
		LeftMin, LeftMax,
		RightMin, RightMax,
		TopMin, TopMax,
		BottomMin, BottomMax,
		Last
	};
	long *strut;
	ulong n;

	if(c->strut == nil)
		free(c->strut);
	c->strut = nil;

	n = getprop_long(&c->w, Net("WM_STRUT_PARTIAL"), "CARDINAL",
		0L, &strut, Last);
	if(n != Last) {
		free(strut);
		n = getprop_long(&c->w, Net("WM_STRUT"), "CARDINAL",
			0L, &strut, 4L);
		if(n != 4) {
			free(strut);
			return;
		}
		Dprint(DEwmh, "ewmh_getstrut(%C[%s]) Using WM_STRUT\n", c, clientname(c));
		strut = erealloc(strut, Last * sizeof *strut);
		strut[LeftMin] = strut[RightMin] = 0;
		strut[LeftMax] = strut[RightMax] = INT_MAX;
		strut[TopMin] = strut[BottomMin] = 0;
		strut[TopMax] = strut[BottomMax] = INT_MAX;
	}
	c->strut = emalloc(sizeof *c->strut);
	c->strut->left =   Rect(0,                strut[LeftMin],  strut[Left],      strut[LeftMax]);
	c->strut->right =  Rect(-strut[Right],    strut[RightMin], 0,                strut[RightMax]);
	c->strut->top =    Rect(strut[TopMin],    0,               strut[TopMax],    strut[Top]);
	c->strut->bottom = Rect(strut[BottomMin], -strut[Bottom],  strut[BottomMax], 0);
	Dprint(DEwmh, "ewmh_getstrut(%C[%s])\n", c, clientname(c));
	Dprint(DEwmh, "\ttop: %R\n", c->strut->top);
	Dprint(DEwmh, "\tleft: %R\n", c->strut->left);
	Dprint(DEwmh, "\tright: %R\n", c->strut->right);
	Dprint(DEwmh, "\tbottom: %R\n", c->strut->bottom);
	free(strut);
	view_update(selview);
}

static void
ewmh_setstate(Client *c, Atom state, int action) {

	Dprint(DEwmh, "\tSTATE = %A\n", state);
	if(state == 0)
		return;

	if(state == STATE("FULLSCREEN"))
		fullscreen(c, action, -1);
	else
	if(state == STATE("DEMANDS_ATTENTION"))
		client_seturgent(c, action, UrgClient);
}

int
ewmh_clientmessage(XClientMessageEvent *e) {
	Client *c;
	View *v;
	ulong *l;
	ulong msg;
	int action, i;

	l = (ulong*)e->data.l;
	msg = e->message_type;
	Dprint(DEwmh, "ClientMessage: %A\n", msg);

	if(msg == NET("WM_STATE")) {
		enum {
			StateUnset,
			StateSet,
			StateToggle,
		};
		if(e->format != 32)
			return -1;
		c = win2client(e->window);
		if(c == nil)
			return 0;
		switch(l[0]) {
		case StateUnset:  action = Off;    break;
		case StateSet:    action = On;     break;
		case StateToggle: action = Toggle; break;
		default: return -1;
		}
		Dprint(DEwmh, "\tAction: %s\n", TOGGLE(action));
		ewmh_setstate(c, l[1], action);
		ewmh_setstate(c, l[2], action);
		return 1;
	}else
	if(msg == NET("ACTIVE_WINDOW")) {
		if(e->format != 32)
			return -1;
		Dprint(DEwmh, "\tsource: %ld\n", l[0]);
		Dprint(DEwmh, "\twindow: 0x%lx\n", e->window);
		c = win2client(e->window);
		if(c == nil)
			return 1;
		Dprint(DEwmh, "\tclient: %s\n", clientname(c));
		if(l[0] != 2)
			return 1;
		focus(c, true);
		return 1;
	}else
	if(msg == NET("CLOSE_WINDOW")) {
		if(e->format != 32)
			return -1;
		Dprint(DEwmh, "\tsource: %ld\n", l[0]);
		Dprint(DEwmh, "\twindow: 0x%lx\n", e->window);
		c = win2client(e->window);
		if(c == nil)
			return 1;
		client_kill(c, true);
		return 1;
	}else
	if(msg == NET("CURRENT_DESKTOP")) {
		if(e->format != 32)
			return -1;
		for(v=view, i=l[0]; v; v=v->next, i--)
			if(i == 0)
				break;
		Dprint(DEwmh, "\t%s\n", v->name);
		if(i == 0)
			view_select(v->name);
		return 1;
	}else
	if(msg == xatom("WM_PROTOCOLS")) {
		if(e->format != 32)
			return 0;
		Dprint(DEwmh, "\t%A\n", l[0]);
		if(l[0] == NET("WM_PING")) {
			if(e->window != scr.root.xid)
				return -1;
			c = win2client(l[2]);
			if(c == nil)
				return 1;
			Dprint(DEwmh, "\tclient = [%C]\"%s\"\n", c, clientname(c));
			Dprint(DEwmh, "\ttimer = %ld, ping = %ld\n",
					c->w.ewmh.timer, c->w.ewmh.ping);
			if(c->w.ewmh.timer)
				ixp_unsettimer(&srv, c->w.ewmh.timer);
			c->w.ewmh.timer = 0;
			c->w.ewmh.ping = 0;
			return 1;
		}
	}

	return 0;
}

void
ewmh_framesize(Client *c) {
	Rectangle r;
	Frame *f;

	f = c->sel;
	r.min.x = f->crect.min.x;
	r.min.y = f->crect.min.y;
	r.max.x = Dx(f->r) - f->crect.max.x;
	r.max.y = Dy(f->r) - f->crect.max.y;

	long extents[] = {
		r.min.x, r.max.x,
		r.min.y, r.max.y,
	};
	changeprop_long(&c->w, Net("FRAME_EXTENTS"), "CARDINAL",
			extents, nelem(extents));
}

void
ewmh_updatestate(Client *c) {
	long state[16];
	Frame *f;
	int i;

	f = c->sel;
	if(f == nil || f->view != selview)
		return;

	i = 0;
	if(f->collapsed)
		state[i++] = STATE("SHADED");
	if(c->fullscreen >= 0)
		state[i++] = STATE("FULLSCREEN");
	if(c->urgent)
		state[i++] = STATE("DEMANDS_ATTENTION");

	if(i > 0)
		changeprop_long(&c->w, Net("WM_STATE"), "ATOM", state, i);
	else
		delproperty(&c->w, Net("WM_STATE"));

	if(c->fullscreen >= 0)
		changeprop_long(&c->w, Net("WM_FULLSCREEN_MONITORS"), "CARDINAL",
				(long[]) { c->fullscreen, c->fullscreen,
					   c->fullscreen, c->fullscreen }, 
				4);
	else
		delproperty(&c->w, Net("WM_FULLSCREEN_MONITORS"));
}

/* Views */
void
ewmh_updateviews(void) {
	View *v;
	Vector_ptr tags;
	long i;

	if(starting)
		return;

	vector_pinit(&tags);
	for(v=view, i=0; v; v=v->next, i++)
		vector_ppush(&tags, v->name);
	vector_ppush(&tags, nil);
	changeprop_textlist(&scr.root, Net("DESKTOP_NAMES"), "UTF8_STRING", (char**)tags.ary);
	changeprop_long(&scr.root, Net("NUMBER_OF_DESKTOPS"), "CARDINAL", &i, 1);
	vector_pfree(&tags);
	ewmh_updateview();
	ewmh_updateclients();
}

static int
viewidx(View *v) {
	View *vp;
	int i;

	for(vp=view, i=0; vp; vp=vp->next, i++)
		if(vp == v)
			break;
	assert(vp);
	return i;
}

void
ewmh_updateview(void) {
	long i;

	if(starting)
		return;

	i = viewidx(selview);
	changeprop_long(&scr.root, Net("CURRENT_DESKTOP"), "CARDINAL", &i, 1);
}

void
ewmh_updateclient(Client *c) {
	long i;

	i = -1;
	if(c->sel)
		i = viewidx(c->sel->view);
	changeprop_long(&c->w, Net("WM_DESKTOP"), "CARDINAL", &i, 1);
}

void
ewmh_updateclients(void) {
	Client *c;

	if(starting)
		return;

	for(c=client; c; c=c->next)
		ewmh_updateclient(c);
}

