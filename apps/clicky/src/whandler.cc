#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif
#include <click/config.h>
#include "whandler.hh"
#include "wdriver.hh"
#include <gdk/gdkkeysyms.h>
#include <click/confparse.hh>
extern "C" {
#include "interface.h"
#include "support.h"
}

typedef RouterWindow::whandler whandler;

namespace {
struct whandler_autorefresh {
    String hname;
    whandler *wh;
    whandler_autorefresh(const String &hname_, whandler *wh_)
	: hname(hname_), wh(wh_) {
    }
};
}

extern "C" {
static gboolean on_handler_event(GtkWidget *, GdkEvent *, gpointer);
static void on_handler_entry_changed(GObject *, GParamSpec *, gpointer);
static void on_handler_text_buffer_changed(GtkTextBuffer *, gpointer);
static void on_handler_check_button_toggled(GtkToggleButton *, gpointer);
static void on_handler_action_apply_clicked(GtkButton *, gpointer);
static void on_handler_action_cancel_clicked(GtkButton *, gpointer);
static gboolean on_handler_autorefresh(gpointer);

static void on_hpref_visible_toggled(GtkToggleButton *, gpointer);
static void on_hpref_refreshable_toggled(GtkToggleButton *, gpointer);
static void on_hpref_autorefresh_toggled(GtkToggleButton *, gpointer);
static void on_hpref_autorefresh_value_changed(GtkSpinButton *, gpointer);
static void on_hpref_preferences_clicked(GtkButton *, gpointer);
static void on_hpref_ok_clicked(GtkButton *, gpointer);
static void on_hpref_cancel_clicked(GtkButton *, gpointer);

static void destroy_callback(GtkWidget *w, gpointer) {
    gtk_widget_destroy(w);
}
static void destroy_autorefresh_callback(gpointer user_data) {
    whandler_autorefresh *wa = reinterpret_cast<whandler_autorefresh *>(user_data);
    delete wa;
}
}

RouterWindow::whandler::whandler(RouterWindow *rw)
    : _rw(rw), _hpref_actions(0), _actions_changed(false), _updating(0)
{
    _handlerbox = GTK_BOX(lookup_widget(_rw->_window, "eview_handlerbox"));

    // config handler entry is always there, thus special
    _eview_config = lookup_widget(_rw->_window, "eview_config");
    g_object_set_data_full(G_OBJECT(_eview_config), "clicky_hname", g_strdup(""), g_free);
    g_signal_connect(_eview_config, "event", G_CALLBACK(on_handler_event), this);
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(_eview_config));
    g_signal_connect(buffer, "changed", G_CALLBACK(on_handler_text_buffer_changed), this);
    g_object_set_data(G_OBJECT(buffer), "clicky_view", _eview_config);

    _actions[0] = _actions[1] = 0;
    _actions_apply[0] = _actions_apply[1] = 0;
}

RouterWindow::whandler::~whandler()
{
}

void RouterWindow::whandler::clear()
{
    _ehandlers.clear();
    _hvalues.clear();
    _hinfo.clear();
    _display_ename = String();
}


/*****
 *
 * Per-hander widget settings
 *
 */

void RouterWindow::whandler::recalculate_positions()
{
    int pos = 0;
    for (std::deque<hinfo>::iterator iter = _hinfo.begin();
	 iter != _hinfo.end(); ++iter) {
	iter->wposition = pos;
	if (iter->flags & (hflag_visible | hflag_preferences))
	    ++pos;
    }
}

void RouterWindow::whandler::hinfo::start_autorefresh(whandler *wh)
{
    if ((flags & hflag_autorefresh)
	&& (flags & hflag_r)
	&& !(flags & hflag_autorefresh_outstanding)
	&& !autorefresh_source
	&& wh->active()) {
	whandler_autorefresh *wa = new whandler_autorefresh(fullname, wh);
	autorefresh_source = g_timeout_add_full
	    (G_PRIORITY_DEFAULT, autorefresh, on_handler_autorefresh, wa,
	     destroy_autorefresh_callback);
    }
}

int RouterWindow::whandler::hinfo::create_preferences(whandler *wh)
{
    assert((flags & hflag_preferences) && !wcontainer && !wlabel && !wdata);

    // set up the frame
    wcontainer = gtk_frame_new(NULL);
    wlabel = gtk_label_new(name.c_str());
    gtk_label_set_attributes(GTK_LABEL(wlabel), wh->router_window()->small_attr());
    gtk_misc_set_alignment(GTK_MISC(wlabel), 0, 0.5);
    gtk_misc_set_padding(GTK_MISC(wlabel), 2, 0);
    gtk_frame_set_label_widget(GTK_FRAME(wcontainer), wlabel);
    GtkWidget *aligner = gtk_alignment_new(0, 0, 1, 1);
    gtk_alignment_set_padding(GTK_ALIGNMENT(aligner), 0, 4, 2, 2);
    gtk_container_add(GTK_CONTAINER(wcontainer), aligner);
    g_object_set_data_full(G_OBJECT(aligner), "clicky_hname", g_strdup(fullname.c_str()), g_free);
    
    // fill the dialog
    GtkWidget *mainbox = gtk_vbox_new(FALSE, 0);
    gtk_container_add(GTK_CONTAINER(aligner), mainbox);

    GtkWidget *w = gtk_check_button_new_with_label(_("Visible"));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w), (flags & hflag_visible));
    gtk_box_pack_start(GTK_BOX(mainbox), w, FALSE, FALSE, 0);
    g_signal_connect(w, "toggled", G_CALLBACK(on_hpref_visible_toggled), wh);

    GtkWidget *visiblebox = gtk_vbox_new(FALSE, 0);
    gtk_box_pack_start(GTK_BOX(mainbox), visiblebox, FALSE, FALSE, 0);
    g_object_set_data(G_OBJECT(w), "clicky_hider", visiblebox);

    GtkWidget *autorefresh_period = 0;

    if (flags & hflag_r) {
	w = gtk_check_button_new_with_label(_("Refreshable"));
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w), (flags & hflag_refresh));
	gtk_box_pack_start(GTK_BOX(visiblebox), w, FALSE, FALSE, 0);
	g_signal_connect(w, "toggled", G_CALLBACK(on_hpref_refreshable_toggled), wh);
	if (flags & hflag_rparam) {
	    // XXX add a text widget for refresh data
	}

	// autorefresh
	w = gtk_check_button_new_with_label(_("Autorefresh"));
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w), (flags & hflag_autorefresh));
	gtk_box_pack_start(GTK_BOX(visiblebox), w, FALSE, FALSE, 0);
	g_signal_connect(w, "toggled", G_CALLBACK(on_hpref_autorefresh_toggled), wh);

	autorefresh_period = gtk_hbox_new(FALSE, 0);
	g_object_set_data(G_OBJECT(w), "clicky_hider", autorefresh_period);
	GtkAdjustment *adj = (GtkAdjustment *) gtk_adjustment_new(autorefresh / 1000., 0.01, 60, 0.01, 0.5, 0.5);
	GtkWidget *spin = gtk_spin_button_new(adj, 0.01, 3);
	gtk_box_pack_start(GTK_BOX(autorefresh_period), spin, FALSE, FALSE, 0);
	g_signal_connect(spin, "value-changed", G_CALLBACK(on_hpref_autorefresh_value_changed), wh);
	GtkWidget *label = gtk_label_new(_(" sec period"));
	gtk_box_pack_start(GTK_BOX(autorefresh_period), label, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(visiblebox), autorefresh_period, FALSE, FALSE, 0);
    }

    // return
    gtk_widget_show_all(wcontainer);
    if ((flags & (hflag_r | hflag_autorefresh)) != (hflag_r | hflag_autorefresh)
	&& autorefresh_period)
	gtk_widget_hide(autorefresh_period);
    if (!(flags & hflag_visible))
	gtk_widget_hide(visiblebox);
    return 0;
}

int RouterWindow::whandler::hinfo::create_display(whandler *wh)
{
    assert(!(flags & hflag_preferences) && !wdata);
    
    // create container
    if (wcontainer)
	/* do not recreate */;
    else if (flags & hflag_collapse)
	wcontainer = gtk_expander_new(NULL);
    else if (flags & (hflag_button | hflag_checkbox))
	wcontainer = gtk_hbox_new(FALSE, 0);
    else
	wcontainer = gtk_vbox_new(FALSE, 0);

    // create label
    if ((flags & hflag_collapse) || !(flags & (hflag_button | hflag_checkbox))) {
	wlabel = gtk_label_new(name.c_str());
	gtk_label_set_attributes(GTK_LABEL(wlabel), wh->router_window()->small_attr());
	if (!(flags & hflag_collapse))
	    gtk_label_set_ellipsize(GTK_LABEL(wlabel), PANGO_ELLIPSIZE_END);
	gtk_misc_set_alignment(GTK_MISC(wlabel), 0, 0.5);
	gtk_widget_show(wlabel);
	if (flags & hflag_collapse)
	    gtk_expander_set_label_widget(GTK_EXPANDER(wcontainer), wlabel);
	else
	    gtk_box_pack_start(GTK_BOX(wcontainer), wlabel, FALSE, FALSE, 0);
    }

    // create data widget
    GtkWidget *wadd;
    int padding = 0;
    gboolean expand = FALSE;
    if (flags & hflag_button) {
	wadd = wdata = gtk_button_new_with_label(name.c_str());
	if (!editable())
	    gtk_widget_set_sensitive(wdata, FALSE);
	else if (wh->active())
	    g_signal_connect(wdata, "clicked", G_CALLBACK(on_handler_action_apply_clicked), wh);
	padding = 2;
	
    } else if (flags & hflag_checkbox) {
	wadd = gtk_event_box_new();
	wdata = gtk_check_button_new_with_label(name.c_str());
	gtk_toggle_button_set_inconsistent(GTK_TOGGLE_BUTTON(wdata), TRUE);
	if (!editable())
	    gtk_widget_set_sensitive(wdata, FALSE);
	else if (wh->active()) {
	    g_signal_connect(wdata, "toggled", G_CALLBACK(on_handler_check_button_toggled), wh);
	    g_object_set_data_full(G_OBJECT(wadd), "clicky_hname", g_strdup(fullname.c_str()), g_free);
	    gtk_widget_add_events(wdata, GDK_FOCUS_CHANGE_MASK);
	    g_signal_connect(wdata, "event", G_CALLBACK(on_handler_event), wh);
	}
	expand = TRUE;
	gtk_container_add(GTK_CONTAINER(wadd), wdata);
	gtk_widget_show(wdata);
	// does nothing yet
	
    } else if (flags & hflag_multiline) {
	wadd = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(wadd), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(wadd), GTK_SHADOW_IN);

	GtkTextBuffer *buffer = gtk_text_buffer_new(wh->router_window()->binary_tag_table());
	if (refreshable())
	    gtk_text_buffer_set_text(buffer, "???", 3);
	wdata = gtk_text_view_new_with_buffer(buffer);
	g_object_set_data(G_OBJECT(buffer), "clicky_view", wdata);
	gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(wdata), GTK_WRAP_WORD);
	gtk_widget_show(wdata);
	gtk_container_add(GTK_CONTAINER(wadd), wdata);

	if (!editable()) {
	    gtk_text_view_set_editable(GTK_TEXT_VIEW(wdata), FALSE);
	    g_object_set(wdata, "can-focus", FALSE, (const char *) NULL);
	} else if (wh->active()) {
	    g_signal_connect(wdata, "event", G_CALLBACK(on_handler_event), wh);
	    g_signal_connect(buffer, "changed", G_CALLBACK(on_handler_text_buffer_changed), wh);
	}
	
    } else {
	wadd = wdata = gtk_entry_new();
	if (refreshable())
	    gtk_entry_set_text(GTK_ENTRY(wdata), "???");

	if (!editable()) {
	    gtk_entry_set_editable(GTK_ENTRY(wdata), FALSE);
	    g_object_set(wdata, "can-focus", FALSE, (const char *) NULL);
	} else if (wh->active()) {
	    g_signal_connect(wdata, "event", G_CALLBACK(on_handler_event), wh);
	    g_signal_connect(wdata, "notify::text", G_CALLBACK(on_handler_entry_changed), wh);
	}
    }

    g_object_set_data_full(G_OBJECT(wdata), "clicky_hname", g_strdup(fullname.c_str()), g_free);
    
    gtk_widget_show(wadd);
    if (flags & hflag_collapse)
	gtk_container_add(GTK_CONTAINER(wcontainer), wadd);
    else
	gtk_box_pack_start(GTK_BOX(wcontainer), wadd, expand, expand, 0);

    // initiate autorefresh
    if (flags & hflag_autorefresh)
	start_autorefresh(wh);
    
    return padding;
}

void RouterWindow::whandler::hinfo::create(whandler *wh, int new_flags)
{
    if (flags & hflag_special)
	return;
    
    // don't flash the expander
    if (wcontainer
	&& (((flags & new_flags) & (hflag_collapse | hflag_visible))
	    == (hflag_collapse | hflag_visible))
	&& (flags & hflag_preferences) == 0
	&& (new_flags & hflag_preferences) == 0) {
	gtk_widget_destroy(gtk_bin_get_child(GTK_BIN(wcontainer)));
	wdata = 0;
    } else if (wcontainer) {
	gtk_widget_destroy(wcontainer);
	wcontainer = wlabel = wdata = 0;
    } else
	assert(wcontainer == 0 && wlabel == 0 && wdata == 0);
    
    // set flags, potentially recalculate positions
    if ((flags ^ new_flags) & (hflag_visible | hflag_preferences)) {
	flags = new_flags;
	wh->recalculate_positions();
    } else
	flags = new_flags;
    
    // create the body
    int padding;
    if (flags & hflag_preferences)
	padding = create_preferences(wh);
    else if (flags & hflag_visible)
	padding = create_display(wh);
    else
	return;
    
    // add to the container
    if (!wcontainer->parent) {
	gtk_box_pack_start(wh->handler_box(), wcontainer, FALSE, FALSE, padding);
	gtk_box_reorder_child(wh->handler_box(), wcontainer, wposition);
	gtk_widget_show(wcontainer);
    }
}

bool binary_to_utf8(const String &data, StringAccum &text, Vector<int> &positions)
{
    static const char hexdigits[] = "0123456789ABCDEF";
    const char *last = data.begin();
    bool multiline = false;
    for (const char *s = data.begin(); s != data.end(); s++)
	if ((unsigned char) *s > 126
	    || ((unsigned char) *s < 32 && !isspace((unsigned char) *s))) {
	    if (last != s)
		text.append(last, s - last);
	    if (positions.size() && positions.back() == text.length())
		positions.pop_back();
	    else
		positions.push_back(text.length());
	    text.append(hexdigits[((unsigned char) *s) >> 4]);
	    text.append(hexdigits[((unsigned char) *s) & 15]);
	    positions.push_back(text.length());
	    last = s + 1;
	    multiline = true;
	} else if (*s == '\n' || *s == '\f')
	    multiline = true;
    if (text.length())
	text.append(last, data.end() - last);
    return multiline;
}

void RouterWindow::whandler::hinfo::display(RouterWindow::whandler *wh, const String &hparam, const String &hvalue, bool change_form)
{
    if (!wdata || (flags & hflag_button))
	return;

    // Multiline data requires special handling
    StringAccum binary_data;
    Vector<int> positions;
    bool multiline = binary_to_utf8(hvalue, binary_data, positions);

    // Valid checkbox data?
    if (flags & hflag_checkbox) {
	bool value;
	if (cp_bool(cp_uncomment(hvalue), &value)) {
	    gtk_toggle_button_set_inconsistent(GTK_TOGGLE_BUTTON(wdata), FALSE);
	    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wdata), value);
	    return;
	} else if (!change_form) {
	    gtk_toggle_button_set_inconsistent(GTK_TOGGLE_BUTTON(wdata), TRUE);
	    return;
	} else
	    create(wh, (flags & ~hflag_checkbox) | (multiline ? hflag_multiline : 0));
    }

    // Valid multiline data?
    if (!(flags & hflag_multiline) && (multiline || positions.size())) {
	if (!change_form) {
	    display(wh, String(), "???", false);
	    return;
	} else
	    create(wh, flags | hflag_multiline);
    }
    
    // Set data
    if (positions.size()) {
	assert(flags & hflag_multiline);
	GtkTextBuffer *b = gtk_text_view_get_buffer(GTK_TEXT_VIEW(wdata));
	gtk_text_buffer_set_text(b, binary_data.data(), binary_data.length());
	GtkTextIter i1, i2;
	for (int *i = positions.begin(); i != positions.end(); i += 2) {
	    gtk_text_buffer_get_iter_at_offset(b, &i1, i[0]);
	    gtk_text_buffer_get_iter_at_offset(b, &i2, i[1]);
	    gtk_text_buffer_apply_tag(b, wh->router_window()->binary_tag(), &i1, &i2);
	}
	gtk_text_buffer_get_end_iter(b, &i1);
	gtk_text_buffer_place_cursor(b, &i1);	
    } else if (flags & hflag_multiline) {
	GtkTextBuffer *b = gtk_text_view_get_buffer(GTK_TEXT_VIEW(wdata));
	gtk_text_buffer_set_text(b, hvalue.data(), hvalue.length());
	GtkTextIter i1;
	gtk_text_buffer_get_end_iter(b, &i1);
	gtk_text_buffer_place_cursor(b, &i1);	
    } else {
	gtk_entry_set_text(GTK_ENTRY(wdata), hvalue.c_str());
	gtk_entry_set_position(GTK_ENTRY(wdata), -1);
    }

    // Display parameters
    if (wlabel && (hparam || (flags & hflag_hparam_displayed)))
	if (!hparam) {
	    gtk_label_set_text(GTK_LABEL(wlabel), name.c_str());
	    flags &= ~hflag_hparam_displayed;
	} else {
	    StringAccum sa;
	    sa << name << ' ' << hparam.substring(0, MIN(hparam.length(), 100));
	    gtk_label_set_text(GTK_LABEL(wlabel), sa.c_str());
	    flags |= hflag_hparam_displayed;
	}

    // Unhighlight
    set_edit_active(wh->router_window(), false);
}



/*****
 *
 * Life is elsewhere
 *
 */

void RouterWindow::whandler::display(const String &ename, bool incremental)
{
    if (_display_ename == ename && incremental)
	return;
    _display_ename = ename;
    gtk_container_foreach(GTK_CONTAINER(_handlerbox), destroy_callback, NULL);
    _hinfo.clear();
    hide_actions();
    _hpref_actions = 0;
    
    String *hdata;
    if (ename)
	hdata = _ehandlers.findp(ename);
    else
	hdata = 0;

    if (!hdata || !*hdata) {
	GtkWidget *w = lookup_widget(_rw->_window, "eview_config");
	gtk_text_view_set_editable(GTK_TEXT_VIEW(w), TRUE);
	g_object_set(G_OBJECT(w), "can-focus", TRUE, (const char *) NULL);
    }
    if (!hdata && _ehandlers.empty())
	// special case: there are no elements
	return;
    else if (!hdata) {
	GtkWidget *l = gtk_label_new(NULL);
	gtk_label_set_markup(GTK_LABEL(l), _("Unknown element"));
	gtk_widget_show(l);
	gtk_box_pack_start(_handlerbox, l, FALSE, FALSE, 0);
	return;
    } else if (!*hdata) {
	assert(_rw->driver());
	GtkWidget *l = gtk_label_new(NULL);
	gtk_label_set_markup(GTK_LABEL(l), _("<i>Loading...</i>"));
	gtk_widget_show(l);
	gtk_box_pack_start(_handlerbox, l, FALSE, FALSE, 0);
	_rw->driver()->do_read(ename + ".handlers", String(), 0);
	return;
    }
    
    // parse handler data into _hinfo
    const char *s = hdata->begin();
    bool syntax_error = false;
    while (s != hdata->end()) {
	const char *hname = s;
	while (s != hdata->end() && !isspace((unsigned char) *s))
	    ++s;
	if (s == hname || s == hdata->end() || *s != '\t') {
	    syntax_error = true;
	    break;
	}
	String n = hdata->substring(hname, s);

	while (s != hdata->end() && isspace((unsigned char) *s))
	    ++s;

	int hflags = 0;
	for (; s != hdata->end() && !isspace((unsigned char) *s); ++s)
	    switch (*s) {
	      case 'r':
		hflags |= hflag_r;
		break;
	      case 'w':
		hflags |= hflag_w;
		break;
	      case '+':
		hflags |= hflag_rparam;
		break;
	      case '%':
		hflags |= hflag_raw;
		break;
	      case '.':
		hflags |= hflag_calm;
		break;
	      case '$':
		hflags |= hflag_expensive;
		break;
	      case 'b':
		hflags |= hflag_button;
		break;
	      case 'c':
		hflags |= hflag_checkbox;
		break;
	    }
	if (!(hflags & hflag_r))
	    hflags &= ~hflag_rparam;
	if (hflags & hflag_r)
	    hflags &= ~hflag_button;
	if (hflags & hflag_rparam)
	    hflags &= ~hflag_checkbox;
	if (hinfo::default_refreshable(hflags))
	    hflags |= hflag_refresh;
	if (n == "class" || n == "name")
	    hflags |= hflag_boring | hflag_special;
	else if (n == "config")
	    hflags |= hflag_multiline | hflag_special;
	else if (n == "ports" || n == "handlers")
	    hflags |= hflag_collapse;

	_hinfo.push_front(hinfo(_display_ename, n, hflags));

	while (s != hdata->end() && *s != '\r' && *s != '\n')
	    ++s;
	if (s + 1 < hdata->end() && *s == '\r' && s[1] == '\n')
	    s += 2;
	else if (s != hdata->end())
	    ++s;
    }

    // parse _hinfo into widgets
    for (size_t i = 0; i < _hinfo.size(); i++) {
	hinfo &hi = _hinfo[i];

	if (hi.flags & hflag_boring)
	    continue;

	if (hi.name == "config") {
	    hi.wdata = _eview_config;
	    gboolean edit = (active() && hi.editable() ? TRUE : FALSE);
	    gtk_text_view_set_editable(GTK_TEXT_VIEW(hi.wdata), edit);
	    g_object_set(hi.wdata, "can-focus", edit, (const char *) NULL);
	    g_object_set_data_full(G_OBJECT(hi.wdata), "clicky_hname", g_strdup(hi.fullname.c_str()), g_free);
	    continue;
	}

	hi.create(this, hi.flags | hflag_visible);

	if (String *x = _hvalues.findp(hi.fullname))
	    if ((hi.flags & hflag_r) && (hi.flags & hflag_calm)) {
		_updating++;
		hi.display(this, String(), *x, true);
		_updating--;
	    } else
		_hvalues.remove(hi.fullname);
    }

    // the final button box
    GtkWidget *bbox = gtk_hbutton_box_new();
    gtk_button_box_set_layout(GTK_BUTTON_BOX(bbox), GTK_BUTTONBOX_END);
    _hpref_actions = GTK_BUTTON_BOX(bbox);
    on_preferences(onpref_initial);
    gtk_box_pack_end(_handlerbox, bbox, FALSE, FALSE, 0);    

    GtkWidget *w = gtk_hseparator_new();
    gtk_box_pack_end(_handlerbox, w, FALSE, FALSE, 4);
    gtk_widget_show(w);
}

void RouterWindow::whandler::on_preferences(int action)
{
    gtk_container_foreach(GTK_CONTAINER(_hpref_actions), destroy_callback, NULL);
    if (action == onpref_initial || action == onpref_prefok
	|| action == onpref_prefcancel) {
	GtkWidget *w = gtk_button_new_from_stock(GTK_STOCK_PREFERENCES);
	gtk_button_set_relief(GTK_BUTTON(w), GTK_RELIEF_NONE);
	gtk_container_add(GTK_CONTAINER(_hpref_actions), w);
	g_signal_connect(w, "clicked", G_CALLBACK(on_hpref_preferences_clicked), this);
    } else {
	GtkWidget *w = gtk_button_new_from_stock(GTK_STOCK_CANCEL);
	gtk_container_add(GTK_CONTAINER(_hpref_actions), w);
	g_signal_connect(w, "clicked", G_CALLBACK(on_hpref_cancel_clicked), this);
	w = gtk_button_new_from_stock(GTK_STOCK_APPLY);
	gtk_container_add(GTK_CONTAINER(_hpref_actions), w);
	g_signal_connect(w, "clicked", G_CALLBACK(on_hpref_ok_clicked), this);
    }
    gtk_widget_show_all(GTK_WIDGET(_hpref_actions));

    if (action == onpref_showpref) {
	for (std::deque<hinfo>::iterator iter = _hinfo.begin();
	     iter != _hinfo.end(); ++iter)
	    iter->create(this, iter->flags | hflag_preferences);
    } else if (action == onpref_prefok || action == onpref_prefcancel) {
	for (std::deque<hinfo>::iterator iter = _hinfo.begin();
	     iter != _hinfo.end(); ++iter)
	    iter->create(this, iter->flags & ~hflag_preferences);
    }
}
    
void RouterWindow::whandler::notify_handlers(const String &ename, const String &data)
{
    if (_ehandlers[ename] != data) {
	_ehandlers.insert(ename, data);
	_hvalues.insert(ename + ".handlers", data);
	if (ename == _display_ename)
	    display(ename, false);
    }
}

void RouterWindow::whandler::make_actions(int which)
{
    assert(which == 0 || which == 1);
    if (_actions[which])
	return;
    // modified from GtkTreeView's search window

    // create window
    _actions[which] = gtk_window_new(GTK_WINDOW_POPUP);
    if (GTK_WINDOW(_rw->_window)->group)
	gtk_window_group_add_window(GTK_WINDOW(_rw->_window)->group, GTK_WINDOW(_actions[which]));
    gtk_window_set_transient_for(GTK_WINDOW(_actions[which]), GTK_WINDOW(_rw->_window));
    gtk_window_set_destroy_with_parent(GTK_WINDOW(_actions[which]), TRUE);
    // gtk_window_set_modal(GTK_WINDOW(tree_view->priv->search_window), TRUE);

    // add contents
    GtkWidget *frame = gtk_frame_new(NULL);
    gtk_frame_set_shadow_type(GTK_FRAME(frame), GTK_SHADOW_ETCHED_IN);
    gtk_widget_show(frame);
    gtk_container_add(GTK_CONTAINER(_actions[which]), frame);

    GtkWidget *bbox = gtk_hbutton_box_new();
    gtk_box_set_spacing(GTK_BOX(bbox), 5);
    gtk_widget_show(bbox);
    gtk_container_set_border_width(GTK_CONTAINER(bbox), 3);
    gtk_container_add(GTK_CONTAINER(frame), bbox);
    GtkWidget *cancel = gtk_button_new_from_stock(GTK_STOCK_CANCEL);
    gtk_widget_show(cancel);
    gtk_container_add(GTK_CONTAINER(bbox), cancel);
    g_signal_connect(cancel, "clicked", G_CALLBACK(on_handler_action_cancel_clicked), this);
    if (which == 0)
	_actions_apply[which] = gtk_button_new_from_stock(GTK_STOCK_APPLY);
    else {
	_actions_apply[which] = gtk_button_new_with_mnemonic(_("_Query"));
	gtk_button_set_image(GTK_BUTTON(_actions_apply[which]), gtk_image_new_from_stock(GTK_STOCK_OK, GTK_ICON_SIZE_BUTTON));
    }
    gtk_widget_show(_actions_apply[which]);
    gtk_container_add(GTK_CONTAINER(bbox), _actions_apply[which]);
    g_signal_connect(_actions_apply[which], "clicked", G_CALLBACK(on_handler_action_apply_clicked), this);

    gtk_widget_realize(_actions[which]);
}

void RouterWindow::whandler::show_actions(GtkWidget *near, const String &hname, bool changed)
{
    if ((hname == _actions_hname && (!changed || _actions_changed))
	|| _updating)
	return;
    
    // find handler
    hinfo *hi = find_hinfo(hname);
    if (!hi || !hi->editable() || !active())
	return;

    // mark change
    if (changed) {
	hi->set_edit_active(router_window(), true);
	if (hname == _actions_hname) {
	    _actions_changed = changed;
	    return;
	}
    }

    // hide old actions, create new ones
    if (_actions_hname)
	hide_actions(_actions_hname);
    _actions_hname = hname;
    _actions_changed = changed;

    // remember checkbox state
    if (hi->flags & hflag_checkbox) {
	GtkToggleButton *b = GTK_TOGGLE_BUTTON(hi->wdata);
	if (gtk_toggle_button_get_inconsistent(b)) {
	    _hvalues.insert(hi->fullname, String());
	    gtk_toggle_button_set_active(b, FALSE);
	} else
	    _hvalues.insert(hi->fullname, cp_unparse_bool(gtk_toggle_button_get_active(b)));
	gtk_toggle_button_set_inconsistent(b, FALSE);
    }
    
    // get monitor and widget coordinates
    gtk_widget_realize(near);    
    GdkScreen *screen = gdk_drawable_get_screen(near->window);
    gint monitor_num = gdk_screen_get_monitor_at_window(screen, near->window);
    GdkRectangle monitor;
    gdk_screen_get_monitor_geometry(screen, monitor_num, &monitor);
    
    while (GTK_WIDGET_NO_WINDOW(near))
	near = near->parent;
    gint near_x1, near_y1, near_x2, near_y2;
    gdk_window_get_origin(near->window, &near_x1, &near_y1);
    gdk_drawable_get_size(near->window, &near_x2, &near_y2);
    near_x2 += near_x1, near_y2 += near_y1;

    // get action area requisition
    int which = (hi->writable() ? 0 : 1);
    make_actions(which);
    GtkRequisition requisition;
    gtk_widget_size_request(_actions[which], &requisition);

    // adjust position based on screen
    gint x, y;
    if (near_x2 > gdk_screen_get_width(screen))
	x = gdk_screen_get_width(screen) - requisition.width;
    else if (near_x2 - requisition.width < 0)
	x = 0;
    else
	x = near_x2 - requisition.width;
    
    if (near_y2 + requisition.height > gdk_screen_get_height(screen)) {
	if (near_y1 - requisition.height < 0)
	    y = 0;
	else
	    y = near_y1 - requisition.height;
    } else
	y = near_y2;

    gtk_window_move(GTK_WINDOW(_actions[which]), x, y);
    gtk_widget_show(_actions[which]);
}

void RouterWindow::whandler::hide_actions(const String &hname, bool restore)
{
    if (!hname || hname == _actions_hname) {
	if (_actions[0])
	    gtk_widget_hide(_actions[0]);
	if (_actions[1])
	    gtk_widget_hide(_actions[1]);
	
	hinfo *hi = find_hinfo(_actions_hname);
	if (!hi || !hi->editable() || !active())
	    return;
	
	// remember checkbox state
	if ((hi->flags & hflag_checkbox) && restore) {
	    GtkToggleButton *b = GTK_TOGGLE_BUTTON(hi->wdata);
	    String v = _hvalues[hi->fullname];
	    bool value;
	    if (cp_bool(v, &value)) {
		_updating++;
		gtk_toggle_button_set_active(b, value);
		_updating--;
	    } else
		gtk_toggle_button_set_inconsistent(b, TRUE);
	}

	// unbold label on empty handlers
	if (hi->write_only() || hi->read_param()) {
	    if (GTK_IS_ENTRY(hi->wdata)
		&& strlen(gtk_entry_get_text(GTK_ENTRY(hi->wdata))) == 0)
		hi->set_edit_active(_rw, false);
	    else if (GTK_IS_TEXT_VIEW(hi->wdata)) {
		GtkTextBuffer *b = gtk_text_view_get_buffer(GTK_TEXT_VIEW(hi->wdata));
		if (gtk_text_buffer_get_char_count(b) == 0)
		    hi->set_edit_active(_rw, false);
	    }
	}
	
	_actions_hname = String();
	_actions_changed = false;
    }
}

void RouterWindow::whandler::apply_action(const String &action_for, bool activate)
{
    if (active()) {
	hinfo *hi = find_hinfo(action_for);
	if (!hi || !hi->editable())
	    return;
	
	int which = (hi->writable() ? 0 : 1);
	if (activate)
	    g_signal_emit_by_name(G_OBJECT(_actions_apply[which]), "activate");

	const gchar *data;
	gchar *data_free = 0;
	if (hi->flags & hflag_button)
	    data = "";
	else if (hi->flags & hflag_checkbox) {
	    if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(hi->wdata)))
		data = "true";
	    else
		data = "false";
	} else if (GTK_IS_TEXT_VIEW(hi->wdata)) {
	    GtkTextIter i1, i2;
	    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(hi->wdata));
	    gtk_text_buffer_get_start_iter(buffer, &i1);
	    gtk_text_buffer_get_end_iter(buffer, &i2);
	    data_free = gtk_text_buffer_get_text(buffer, &i1, &i2, FALSE);
	    data = data_free;
	} else if (GTK_IS_ENTRY(hi->wdata))
	    data = gtk_entry_get_text(GTK_ENTRY(hi->wdata));
	else
	    data = "";

	assert(_rw->driver());
	if (hi->writable())
	    _rw->driver()->do_write(action_for, data, 0);
	else
	    _rw->driver()->do_read(action_for, data, 0);
	_hvalues.remove(hi->fullname);
	
	hide_actions(action_for, false);
	if (data_free)
	    g_free(data_free);
    }
}

extern "C" {
static gboolean on_handler_event(GtkWidget *w, GdkEvent *event, gpointer user_data)
{
    RouterWindow::whandler *wh = reinterpret_cast<RouterWindow::whandler *>(user_data);
    const gchar *hname = (const gchar *) g_object_get_data(G_OBJECT(w), "clicky_hname");
    
    if ((event->type == GDK_FOCUS_CHANGE && !event->focus_change.in)
	|| (event->type == GDK_KEY_PRESS && event->key.keyval == GDK_Escape))
	wh->hide_actions(hname);
    else if ((event->type == GDK_KEY_PRESS && event->key.keyval == GDK_Return)
	     || (event->type == GDK_KEY_PRESS && event->key.keyval == GDK_ISO_Enter)) {
	if (GTK_IS_ENTRY(w))
	    wh->apply_action(hname, true);
    } else if (event->type == GDK_BUTTON_PRESS
	     || event->type == GDK_2BUTTON_PRESS
	     || event->type == GDK_3BUTTON_PRESS)
	wh->show_actions(w, hname, false);

    return FALSE;
}

static void on_handler_check_button_toggled(GtkToggleButton *tb, gpointer user_data)
{
    const gchar *hname = (const gchar *) g_object_get_data(G_OBJECT(tb), "clicky_hname");
    reinterpret_cast<RouterWindow::whandler *>(user_data)->show_actions(GTK_WIDGET(tb), hname, true);
}

static void on_handler_entry_changed(GObject *obj, GParamSpec *, gpointer user_data)
{
    const gchar *hname = (const gchar *) g_object_get_data(obj, "clicky_hname");
    RouterWindow::whandler *wh = reinterpret_cast<RouterWindow::whandler *>(user_data);
    wh->show_actions(GTK_WIDGET(obj), hname, true);
}

static void on_handler_text_buffer_changed(GtkTextBuffer *buffer, gpointer user_data)
{
    GtkWidget *view = (GtkWidget *) g_object_get_data(G_OBJECT(buffer), "clicky_view");
    const gchar *hname = (const gchar *) g_object_get_data(G_OBJECT(view), "clicky_hname");
    RouterWindow::whandler *wh = reinterpret_cast<RouterWindow::whandler *>(user_data);
    wh->show_actions(view, hname, true);
}

static void on_handler_action_cancel_clicked(GtkButton *, gpointer user_data)
{
    reinterpret_cast<RouterWindow::whandler *>(user_data)->hide_actions();
}

static void on_handler_action_apply_clicked(GtkButton *button, gpointer user_data)
{
    RouterWindow::whandler *wh = reinterpret_cast<RouterWindow::whandler *>(user_data);
    const gchar *hname = (const gchar *) g_object_get_data(G_OBJECT(button), "clicky_hname");
    String hstr = (hname ? String(hname) : wh->active_action());
    wh->apply_action(hstr, false);
}
}

/** @brief Read all read handlers and reset all write-only handlers. */
void RouterWindow::whandler::refresh_all()
{
    for (std::deque<hinfo>::iterator hi = _hinfo.begin(); hi != _hinfo.end(); ++hi)
	if (hi->refreshable()) {
	    int flags = (hi->flags & hflag_raw ? 0 : wdriver::dflag_nonraw);
	    _rw->driver()->do_read(hi->fullname, String(), flags);
	} else if (hi->write_only() && _actions_hname != hi->fullname)
	    hi->display(this, String(), String(), false);
}

void RouterWindow::whandler::notify_read(const String &hname, const String &hparam, const String &hvalue, bool real)
{
    if (_display_ename.length() >= hname.length()
	|| memcmp(hname.data(), _display_ename.data(), _display_ename.length()) != 0)
	return;
    hinfo *hi = find_hinfo(hname);
    if (hi && hi->readable()) {
	_updating++;
	hi->display(this, hparam, hvalue, true);
	_updating--;
	
	// set sticky value
	if (real)
	    _hvalues.insert(hi->fullname, hvalue);
	
	// restart autorefresh
	if (hi->flags & hflag_autorefresh_outstanding) {
	    hi->flags &= ~hflag_autorefresh_outstanding;
	    hi->start_autorefresh(this);
	}
    }
}

void RouterWindow::whandler::notify_write(const String &hname, const String &, int status)
{
    if (_display_ename.length() >= hname.length()
	|| memcmp(hname.data(), _display_ename.data(), _display_ename.length()) != 0)
	return;
    hinfo *hi = find_hinfo(hname);
    if (hi && hi->writable()) {
	_updating++;
	if (!hi->readable() && status < 300)
	    hi->display(this, String(), String(), false);
	_updating--;
    }
}

bool RouterWindow::whandler::on_autorefresh(const String &hname)
{
    hinfo *hi = find_hinfo(hname);
    if (hi && hi->readable() && (hi->flags & hflag_autorefresh)
	&& !(hi->flags & hflag_autorefresh_outstanding) && active()) {
	hi->flags |= hflag_autorefresh_outstanding;
	int flags = (hi->flags & hflag_raw ? 0 : wdriver::dflag_nonraw);
	_rw->driver()->do_read(hi->fullname, String(), flags);
	return TRUE;
    } else {
	hi->autorefresh_source = 0;
	return FALSE;
    }
}


/*****
 *
 * Handler preferences
 *
 */

void RouterWindow::whandler::set_hinfo_flags(const String &hname, int flags, int flag_value)
{
    if (hinfo *hi = find_hinfo(hname))
	hi->flags = (hi->flags & ~flags) | (flags & flag_value);
}

void RouterWindow::whandler::set_hinfo_autorefresh_period(const String &hname, int period)
{
    if (hinfo *hi = find_hinfo(hname))
	hi->autorefresh = (period > 0 ? period : 1);
}

const gchar *RouterWindow::whandler::hpref_handler_name(GtkWidget *w)
{
    gpointer x;
    while (w) {
	while (w && !GTK_IS_ALIGNMENT(w))
	    w = w->parent;
	if (w && (x = g_object_get_data(G_OBJECT(w), "clicky_hname")))
	    return reinterpret_cast<gchar *>(x);
	if (w)
	    w = w->parent;
    }
    return "";
}

extern "C" {
static void on_hpref_visible_toggled(GtkToggleButton *button, gpointer user_data)
{
    whandler *wh = reinterpret_cast<whandler *>(user_data);
    const gchar *hname = whandler::hpref_handler_name(GTK_WIDGET(button));
    gboolean on = gtk_toggle_button_get_active(button);
    wh->set_hinfo_flags(hname, whandler::hflag_visible, on ? whandler::hflag_visible : 0);
    
    GtkWidget *widget = reinterpret_cast<GtkWidget *>(g_object_get_data(G_OBJECT(button), "clicky_hider"));
    if (on)
	gtk_widget_show(widget);
    else
	gtk_widget_hide(widget);
}

static void on_hpref_refreshable_toggled(GtkToggleButton *button, gpointer user_data)
{
    whandler *wh = reinterpret_cast<whandler *>(user_data);
    const gchar *hname = whandler::hpref_handler_name(GTK_WIDGET(button));
    gboolean on = gtk_toggle_button_get_active(button);
    wh->set_hinfo_flags(hname, whandler::hflag_refresh, on ? whandler::hflag_refresh : 0);
}

static void on_hpref_autorefresh_toggled(GtkToggleButton *button, gpointer user_data)
{
    whandler *wh = reinterpret_cast<whandler *>(user_data);
    const gchar *hname = whandler::hpref_handler_name(GTK_WIDGET(button));
    gboolean on = gtk_toggle_button_get_active(button);
    wh->set_hinfo_flags(hname, whandler::hflag_autorefresh, on ? whandler::hflag_autorefresh : 0);
    
    GtkWidget *widget = reinterpret_cast<GtkWidget *>(g_object_get_data(G_OBJECT(button), "clicky_hider"));
    if (on)
	gtk_widget_show(widget);
    else
	gtk_widget_hide(widget);
}

static void on_hpref_autorefresh_value_changed(GtkSpinButton *button, gpointer user_data)
{
    whandler *wh = reinterpret_cast<whandler *>(user_data);
    const gchar *hname = whandler::hpref_handler_name(GTK_WIDGET(button));
    wh->set_hinfo_autorefresh_period(hname, (guint) (gtk_spin_button_get_value(button) * 1000));
}

static void on_hpref_preferences_clicked(GtkButton *, gpointer user_data)
{
    whandler *wh = reinterpret_cast<whandler *>(user_data);
    wh->on_preferences(whandler::onpref_showpref);
}

static void on_hpref_ok_clicked(GtkButton *, gpointer user_data)
{
    whandler *wh = reinterpret_cast<whandler *>(user_data);
    wh->on_preferences(whandler::onpref_prefok);
}

static void on_hpref_cancel_clicked(GtkButton *, gpointer user_data)
{
    whandler *wh = reinterpret_cast<whandler *>(user_data);
    wh->on_preferences(whandler::onpref_prefcancel);
}

static gboolean on_handler_autorefresh(gpointer user_data)
{
    whandler_autorefresh *wa = reinterpret_cast<whandler_autorefresh *>(user_data);
    return wa->wh->on_autorefresh(wa->hname);
}
}
