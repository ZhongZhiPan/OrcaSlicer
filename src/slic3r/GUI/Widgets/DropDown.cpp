#include "DropDown.hpp"
#include "DropDownModel.hpp"
#include "Label.hpp"

#include <algorithm>
#include <set>

#include <wx/display.h>
#include <wx/dcbuffer.h>
#include <wx/dcgraph.h>
#include <wx/weakref.h>

#ifdef __WXGTK__
#include <gtk/gtk.h>
#endif

wxDEFINE_EVENT(EVT_DISMISS, wxCommandEvent);

BEGIN_EVENT_TABLE(DropDown, PopupWindow)

EVT_LEFT_DOWN(DropDown::mouseDown)
EVT_LEFT_UP(DropDown::mouseReleased)
EVT_MOUSE_CAPTURE_LOST(DropDown::mouseCaptureLost)
EVT_MOTION(DropDown::mouseMove)
EVT_MOUSEWHEEL(DropDown::mouseWheelMoved)

// catch paint events
EVT_PAINT(DropDown::paintEvent)

END_EVENT_TABLE()

/*
 * Called by the system of by wxWidgets when the panel needs
 * to be redrawn. You can also trigger this call by
 * calling Refresh()/Update().
 */

DropDown::DropDown(std::vector<wxString>&           texts,
                   std::vector<wxString>&           tips,
                   std::vector<wxBitmap>&           icons,
                   std::vector<wxString>&           groups,
                   std::vector<int>&                item_styles,
                   std::map<wxString, DDGroupMeta>& group_metas)
    : texts(texts)
    , tips(tips)
    , icons(icons)
    , groups(groups)
    , item_styles(item_styles)
    , group_metas(group_metas)
    , state_handler(this)
    , border_color(0xDBDBDB)
    , text_color(0x363636)
    , selector_border_color(std::make_pair(0x009688, (int) StateColor::Hovered), std::make_pair(*wxWHITE, (int) StateColor::Normal))
    , selector_background_color(std::make_pair(0xBFE1DE, (int) StateColor::Checked), // ORCA updated background color for checked item
                                std::make_pair(*wxWHITE, (int) StateColor::Normal))
{
    dismissTime = boost::posix_time::microsec_clock::universal_time() - boost::posix_time::seconds(1);
}

DropDown::DropDown(wxWindow*                        parent,
                   std::vector<wxString>&           texts,
                   std::vector<wxString>&           tips,
                   std::vector<wxBitmap>&           icons,
                   std::vector<wxString>&           groups,
                   std::vector<int>&                item_styles,
                   std::map<wxString, DDGroupMeta>& group_metas,
                   long                             style)
    : DropDown(texts, tips, icons, groups, item_styles, group_metas)
{
    Create(parent, style);
}

DropDown::~DropDown()
{
    if (subDropDown) {
        subDropDown->mainDropDown = nullptr;
        delete subDropDown;
    }
    delete tip_window;
}

void DropDown::Popup(wxWindow* focus)
{
    popup_active = true;
    PopupWindow::Popup(focus);
    SetFocus();
}

void DropDown::close_popup()
{
    if (mainDropDown) {
        mainDropDown->close_popup();
        return;
    }
    if (subDropDown) {
        subDropDown->clear_tip();
        subDropDown->popup_active = false;
        subDropDown->PopupWindow::Dismiss();
        subDropDown->group.clear();
    }
    PopupWindow::Dismiss();
    OnDismiss();
}

void DropDown::Create(wxWindow *     parent,
         long           style)
{
    PopupWindow::Create(parent, wxPU_CONTAINS_CONTROLS);
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    SetBackgroundColour(*wxWHITE);
    state_handler.attach({&border_color, &text_color, &selector_border_color, &selector_background_color});
    state_handler.update_binds();
    if ((style & DD_NO_CHECK_ICON) == 0)
        check_bitmap = ScalableBitmap(this, "checked", 16);
    arrow_bitmap = ScalableBitmap(this, "hms_arrow", 16);
    text_off = style & DD_NO_TEXT;

    // BBS set default font
    SetFont(Label::Body_14);
    Bind(wxEVT_LEAVE_WINDOW, [this](wxMouseEvent& event) {
        clear_tip();
        event.Skip();
    });
    Bind(wxEVT_SYS_COLOUR_CHANGED, [this](wxSysColourChangedEvent& event) {
        Invalidate();
        Refresh();
        event.Skip();
    });
    Bind(wxEVT_CHAR_HOOK, [this](wxKeyEvent& event) {
        if (event.GetKeyCode() == WXK_ESCAPE)
            close_popup();
        else
            event.Skip();
    });
#ifdef __WXOSX__
    // PopupWindow releases mouse on idle, which may cause various problems,
    //  such as losting mouse move, and dismissing soon on first LEFT_DOWN event.
    Bind(wxEVT_IDLE, [] (wxIdleEvent & evt) {});
#endif
}

void DropDown::Invalidate(bool clear)
{
    if (popup_active)
        close_popup();
    if (clear) {
        selection = hover_item = -1;
        offset = wxPoint();
    }
    if (selection >= (int) texts.size())
        selection = -1;
    visible_count = -1; // recompute in messureSize()
    m_view_to_real.clear();
    m_real_to_view.clear();
    m_group_members.clear();
    clear_tip();
    // The submenu shares these arrays: reset it on clear, remeasure it otherwise.
    if (subDropDown) {
        if (clear) {
            subDropDown->group.clear();
            subDropDown->selection = subDropDown->hover_item = -1;
            subDropDown->offset                              = wxPoint();
            if (subDropDown->IsShown())
                subDropDown->Dismiss();
        }
        subDropDown->visible_count = -1;
        subDropDown->m_view_to_real.clear();
        subDropDown->m_real_to_view.clear();
        subDropDown->m_group_members.clear();
        subDropDown->need_sync = true;
    }
    need_sync = true;
}

void DropDown::SetSelection(int n)
{
    if (n < 0 || n >= (int) texts.size())
        n = -1;
    if (selection == n) return;
    selection = n;
    if (need_sync) { // for icon Size
        messureSize();
        need_sync = true;
    }
    if (subDropDown)
        subDropDown->SetSelection(n);
    paintNow();
}

wxString DropDown::GetValue() const
{
    return selection >= 0 ? texts[selection] : wxString();
}

void DropDown::SetValue(const wxString &value)
{
    auto i = std::find(texts.begin(), texts.end(), value);
    selection = i == texts.end() ? -1 : std::distance(texts.begin(), i);
    if (!isSelectable(selection))
        selection = -1;
}

void DropDown::SetCornerRadius(double radius)
{
    this->radius = radius;
    paintNow();
}

void DropDown::SetBorderColor(StateColor const &color)
{
    border_color = color;
    state_handler.update_binds();
    paintNow();
}

void DropDown::SetSelectorBorderColor(StateColor const &color)
{
    selector_border_color = color;
    state_handler.update_binds();
    paintNow();
}

void DropDown::SetTextColor(StateColor const &color)
{
    text_color = color;
    state_handler.update_binds();
    paintNow();
}

void DropDown::SetSelectorBackgroundColor(StateColor const &color)
{
    selector_background_color = color;
    state_handler.update_binds();
    paintNow();
}

void DropDown::SetUseContentWidth(bool use, bool limit_max_content_width)
{
    if (use_content_width == use)
        return;
    use_content_width = use;
    this->limit_max_content_width = limit_max_content_width;
    need_sync = true;
    messureSize();
}

void DropDown::SetAlignIcon(bool align) { align_icon = align; }

void DropDown::Rescale()
{
    close_popup();
    clear_tip();
    // ScalableBitmap caches its raster at the old DPI.
    if (check_bitmap.bmp().IsOk())
        check_bitmap.msw_rescale();
    if (arrow_bitmap.bmp().IsOk())
        arrow_bitmap.msw_rescale();
    if (subDropDown)
        subDropDown->Rescale();
    visible_count = -1;
    need_sync = true;
}

bool DropDown::HasDismissLongTime()
{
    auto now = boost::posix_time::microsec_clock::universal_time();
    return !IsShown() &&
        (now - dismissTime).total_milliseconds() >= 20;
}

void DropDown::paintEvent(wxPaintEvent& evt)
{
    // depending on your system you may need to look at double-buffered dcs
    wxBufferedPaintDC dc(this);
    render(dc);
}

/*
 * Alternatively, you can use a clientDC to paint on the panel
 * at any time. Using this generally does not free you from
 * catching paint events, since it is possible that e.g. the window
 * manager throws away your drawing when the window comes to the
 * background, and expects you will redraw it when the window comes
 * back (by sending a paint event).
 */
void DropDown::paintNow()
{
    // depending on your system you may need to look at double-buffered dcs
    //wxClientDC dc(this);
    //render(dc);
    Refresh();
}

static wxSize GetBmpSize(wxBitmap & bmp)
{
#ifdef __APPLE__
    return bmp.GetScaledSize();
#else
    return bmp.GetSize();
#endif
}

/*
 * Here we do the actual rendering. I put it in a separate
 * method so that it can work no matter what type of DC
 * (e.g. wxPaintDC or wxClientDC) is used.
 */
static void _DrawSplitItem(const wxWindow* w, wxDC& dc, wxString split_text, wxPoint start_pt, int item_width, int item_height)
{
    auto pre_clr = dc.GetTextForeground();
    auto pre_pen = dc.GetPen();
    dc.SetTextForeground(StateColor::darkModeColorFor(wxColour(172, 172, 172)));
    dc.SetPen(StateColor::darkModeColorFor(wxColour(166, 169, 170)));
    auto font = w->GetFont();
    font.SetPointSize(font.GetPointSize() - 3);
    dc.SetFont(font);

    int spacing = w->FromDIP(8);

    if (!split_text.empty()) {
        int    max_content_width = item_width - start_pt.x - 2 * spacing;
        wxSize tSize             = dc.GetMultiLineTextExtent(split_text);
        if (tSize.x > max_content_width) {
            split_text = wxControl::Ellipsize(split_text, dc, wxELLIPSIZE_END, max_content_width);
            tSize      = dc.GetMultiLineTextExtent(split_text);
        }

        dc.SetFont(font);
        dc.DrawText(split_text, start_pt);

        int line_width = item_width - start_pt.x - tSize.x - 2 * spacing;
        int line_y     = start_pt.y + (tSize.GetHeight() / 2);
        dc.DrawLine(start_pt.x + tSize.x + spacing, line_y, start_pt.x + tSize.x + line_width + spacing, line_y);
    } else {
        int line_y     = start_pt.y + (item_height / 2);
        int line_width = item_width - start_pt.x - spacing;
        dc.DrawLine(start_pt.x, line_y, start_pt.x + line_width, line_y);
    }
    dc.SetTextForeground(pre_clr);
    dc.SetPen(pre_pen);
    dc.SetFont(w->GetFont());
}

wxString DropDown::displayed_text(size_t i) const
{
    if (groups[i].IsEmpty())
        return texts[i];
    auto it = group_metas.find(groups[i]);
    if (group.IsEmpty())
        return it != group_metas.end() ? it->second.label : groups[i];
    if (it != group_metas.end() && it->second.strip_prefix) {
        const wxString& label = it->second.label;
        if (texts[i].StartsWith(label)) {
            wxString rest = texts[i].substr(label.size());
            if (!rest.IsEmpty() && (rest[0] == ' ' || rest[0] == '\t'))
                return rest.substr(1);
        }
    }
    return texts[i];
}

void DropDown::render(wxDC &dc)
{
    messureSize();
    if (texts.size() == 0) return;
    int states = state_handler.states();
    dc.SetPen(wxPen(border_color.colorForStates(states)));
    dc.SetBrush(wxBrush(StateColor::darkModeColorFor(GetBackgroundColour())));
    // if (GetWindowStyle() & wxBORDER_NONE)
    //    dc.SetPen(wxNullPen);

    // draw background
    wxSize size = GetSize();
    if (radius == 0)
        dc.DrawRectangle(0, 0, size.x, size.y);
    else
        dc.DrawRoundedRectangle(0, 0, size.x, size.y, radius);

    int selected_item = selectedItem();
    int hover_index   = hoverIndex();

    // draw hover rectangle
    wxRect rcContent = {{0, offset.y}, rowSize};
    if (hover_item >= 0 && (states & StateColor::Hovered) &&
        (hover_index < 0 || !(item_styles[hover_index] & (DD_ITEM_STYLE_SPLIT_ITEM | DD_ITEM_STYLE_DISABLED)))) {
        rcContent.y += rowSize.y * hover_item;
        if (rcContent.GetBottom() > 0 && rcContent.y < size.y) {
            if (selected_item == hover_item)
                dc.SetBrush(wxBrush(selector_background_color.colorForStates(states | StateColor::Checked)));
            dc.SetPen(wxPen(selector_border_color.colorForStates(states)));
            rcContent.Deflate(4, 1);
            dc.DrawRectangle(rcContent);
            rcContent.Inflate(4, 1);
        }
        rcContent.y = offset.y;
    }
    // draw checked rectangle
    if (selected_item >= 0 && (selected_item != hover_item || (states & StateColor::Hovered) == 0)) {
        rcContent.y += rowSize.y * selected_item;
        if (rcContent.GetBottom() > 0 && rcContent.y < size.y) {
            dc.SetBrush(wxBrush(selector_background_color.colorForStates(states | StateColor::Checked)));
            dc.SetPen(wxPen(selector_background_color.colorForStates(states)));
            rcContent.Deflate(4, 1);
            dc.DrawRectangle(rcContent);
            rcContent.Inflate(4, 1);
        }
        rcContent.y = offset.y;
    }
    dc.SetBrush(*wxTRANSPARENT_BRUSH);
    {
        wxSize offset = (rowSize - textSize) / 2;
        rcContent.Deflate(0, offset.y);
    }

    // draw position bar
    if (rowSize.y * visible_count > size.y) {
        int    height = rowSize.y * visible_count;
        wxRect rect = {size.x - 6, -offset.y * size.y / height, 4,
                       size.y * size.y / height};
        dc.SetPen(wxPen(border_color.defaultColor()));
        dc.SetBrush(wxBrush(*wxLIGHT_GREY));
        dc.DrawRoundedRectangle(rect, 2);
        rcContent.width -= 6;
    }

    // draw check icon
    rcContent.x += 5;
    rcContent.width -= 5;
    if (check_bitmap.bmp().IsOk()) {
        auto szBmp = check_bitmap.GetBmpSize();
        if (selected_item >= 0) {
            wxPoint pt = rcContent.GetLeftTop();
            pt.y += (rcContent.height - szBmp.y) / 2;
            pt.y += rowSize.y * selected_item;
            if (pt.y + szBmp.y > 0 && pt.y < size.y)
                dc.DrawBitmap(check_bitmap.bmp(), pt);
        }
        rcContent.x += szBmp.x + 5;
        rcContent.width -= szBmp.x + 5;
    }
    // draw texts & icons
    m_truncated.assign(texts.size(), 0);
    for (int index = 0; index < (int) m_view_to_real.size(); ++index) {
        const int encoded   = m_view_to_real[index];
        const int i         = encoded < -1 ? -encoded - 2 : encoded;
        int       states2   = states;
        bool      is_dimmed = (item_styles[i] & DD_ITEM_STYLE_DIMMED) != 0;
        if ((item_styles[i] & DD_ITEM_STYLE_DISABLED) != 0)
            states2 &= ~StateColor::Enabled;
        if (rcContent.GetBottom() < 0) {
            rcContent.y += rowSize.y;
            continue;
        }
        if (rcContent.y > size.y) break;
        wxPoint pt   = rcContent.GetLeftTop();

        if (item_styles[i] & DD_ITEM_STYLE_SPLIT_ITEM) {
            _DrawSplitItem(this, dc, texts[i], pt, rowSize.GetWidth(), rowSize.GetHeight());
            rcContent.y += rowSize.GetHeight();
            continue;
        }

        const bool is_top_level_group = group.IsEmpty() && !groups[i].IsEmpty();
        auto & icon = icons[i];
        auto       size2              = icon.IsOk() ? GetBmpSize(icon) : wxSize();
        if (iconSize.x > 0) {
            if (!is_top_level_group && icon.IsOk()) {
                pt.y += (rcContent.height - size2.y) / 2;
                dc.DrawBitmap(icon, pt);
            }
            pt.x += iconSize.x + 5;
            pt.y = rcContent.y;
        } else if (!is_top_level_group && icon.IsOk()) {
            pt.y += (rcContent.height - size2.y) / 2;
            dc.DrawBitmap(icon, pt);
            pt.x += size2.x + 5;
            pt.y = rcContent.y;
        }
        auto text = displayed_text(i);
        if (!text_off && !text.IsEmpty()) {
            wxSize tSize = dc.GetMultiLineTextExtent(text);
            const int text_right = rcContent.GetRight() - (is_top_level_group ? arrow_bitmap.GetBmpWidth() + 10 : 0);
            if (pt.x + tSize.x > text_right) {
                m_truncated[i] = 1;
                text           = wxControl::Ellipsize(text, dc, wxELLIPSIZE_END, std::max(1, text_right - pt.x));
            }
            pt.y += (rcContent.height - textSize.y) / 2;
            dc.SetFont(GetFont());
            dc.SetTextForeground(is_dimmed ? wxColour(0xCE, 0xCE, 0xCE) : text_color.colorForStates(states2));
            dc.DrawText(text, pt);
            if (is_top_level_group) {
                auto szBmp = arrow_bitmap.GetBmpSize();
                pt.x       = rcContent.GetRight() - szBmp.x - 5;
                pt.y       = rcContent.y + (rcContent.height - szBmp.y) / 2;
                dc.DrawBitmap(arrow_bitmap.bmp(), pt);
            }
        }
        rcContent.y += rowSize.y;
    }
}

void DropDown::messureSize()
{
    if (!need_sync) return;
    textSize = wxSize();
    iconSize = wxSize();
    visible_count = 0;
    wxClientDC dc(GetParent() ? GetParent() : this);
    m_view_to_real.clear();
    auto view         = build_dropdown_view(groups, group);
    m_view_to_real    = std::move(view.rows);
    m_real_to_view    = std::move(view.real_to_view);
    m_group_members   = std::move(view.members);
    m_groups_present  = group.empty() && !m_group_members.empty();
    visible_count     = int(m_view_to_real.size());
    auto measure_item = [&](size_t i) {
        const bool header_row = group.IsEmpty() && !groups[i].IsEmpty();
        wxSize     size1;
        if (!text_off) {
            size1 = dc.GetMultiLineTextExtent(displayed_text(i));
            if (group.IsEmpty() && !groups[i].IsEmpty())
                size1.x += 5 + arrow_bitmap.GetBmpWidth();
        }
        if (!header_row && icons[i].IsOk()) {
            wxSize size2 = GetBmpSize(icons[i]);
            if (size2.x > iconSize.x)
                iconSize = size2;
            if (!align_icon) {
                size1.x += size2.x + (text_off ? 0 : 5);
            }
        }
        if (size1.x > textSize.x)
            textSize = size1;
    };
    for (int row : m_view_to_real)
        measure_item(size_t(row < -1 ? -row - 2 : row));
    if (!align_icon) iconSize.x = 0;
    wxSize szContent = textSize;
    szContent.x += 10;
    if (check_bitmap.bmp().IsOk()) {
        auto szBmp = check_bitmap.GetBmpSize();
        szContent.x += szBmp.x + 5;
    }
    if (iconSize.x > 0) szContent.x += iconSize.x + (text_off ? 0 : 5);
    if (iconSize.y > szContent.y) szContent.y = iconSize.y;
    szContent.y += 10;
    if (visible_count > (size_t) max_visible_rows)
        szContent.x += 6;
    if (GetParent() && group.IsEmpty()) {
        auto x = GetParent()->GetSize().x;
        if (!use_content_width || x > szContent.x)
            szContent.x = x;
    }
    rowSize = szContent;
    if (limit_max_content_width) {
        wxSize parent_size = GetParent()->GetSize();
        if (rowSize.x > parent_size.x * 2) {
            rowSize.x = 2 * parent_size.x;
            szContent = rowSize;
        }
    }
    szContent.y *= std::min((size_t) max_visible_rows, std::max((size_t) visible_count, (size_t) 1));
    szContent.y += visible_count > (size_t) max_visible_rows ? rowSize.y / 2 : 0;
    wxWindow::SetSize(szContent);
    offset.y = clamp_dropdown_offset(offset.y, szContent.y, rowSize.y, visible_count);
#ifdef __WXGTK__
    // Gtk has a wrapper window for popup widget
    // Fix for GNOME Platform 48 X11 backend: ensure size is valid before calling gtk_window_resize
    int gtk_width = szContent.x;
    int gtk_height = szContent.y;
    if (gtk_width <= 0) gtk_width = 100;
    if (gtk_height <= 0) gtk_height = 100;
    gtk_window_resize(GTK_WINDOW(m_widget), gtk_width, gtk_height);
#endif
    if (hasGroups() && subDropDown == nullptr) {
        subDropDown                          = new DropDown(texts, tips, icons, groups, item_styles, group_metas);
        subDropDown->mainDropDown            = this;
        subDropDown->check_bitmap            = check_bitmap;
        subDropDown->arrow_bitmap            = arrow_bitmap;
        subDropDown->text_off                = text_off;
        subDropDown->align_icon              = align_icon;
        subDropDown->use_content_width       = true;
        subDropDown->limit_max_content_width = true;
        subDropDown->max_visible_rows        = 8;
        // Callers may have customized the main popup's appearance.
        subDropDown->radius                    = radius;
        subDropDown->text_color                = text_color;
        subDropDown->border_color              = border_color;
        subDropDown->selector_border_color     = selector_border_color;
        subDropDown->selector_background_color = selector_background_color;
        subDropDown->Create(GetParent(), (text_off ? DD_NO_TEXT : 0) | (check_bitmap.bmp().IsOk() ? 0 : DD_NO_CHECK_ICON));
        subDropDown->selection = selection;
        subDropDown->SetFont(GetFont());
        subDropDown->Bind(wxEVT_COMBOBOX, [this](wxCommandEvent& e) {
            e.SetEventObject(this);
            e.SetId(GetId());
            GetEventHandler()->ProcessEvent(e);
        });
    }
    need_sync = false;
}

bool DropDown::hasGroups() const
{
    if (!group.IsEmpty())
        return false;
    return m_groups_present;
}

bool DropDown::isSelectable(int real_index) const { return dropdown_item_selectable(real_index, item_styles); }

int DropDown::hoverIndex()
{
    if (hover_item < 0 || hover_item >= (int) m_view_to_real.size())
        return -1;
    return m_view_to_real[hover_item];
}

int DropDown::selectedItem()
{
    if (selection < 0 || selection >= (int) m_real_to_view.size())
        return -1;
    return m_real_to_view[selection];
}

void DropDown::autoPosition()
{
    messureSize();
    wxPoint pos;
    wxSize  off;
    if (mainDropDown) {
        pos = mainDropDown->ClientToScreen(wxPoint(0, 0));
        off = mainDropDown->GetSize();
        pos.x += 6;
        pos.y += mainDropDown->hover_item * mainDropDown->rowSize.y + mainDropDown->offset.y;
        off.x -= 12;
        off.y = 0;
    } else {
        pos = GetParent()->ClientToScreen(wxPoint(0, -6));
        off = {0, GetParent()->GetSize().y + 12};
    }
    wxPoint old = GetPosition();
    wxSize size = GetSize();
    Position(pos, off);
    if (old != GetPosition()) {
        size = rowSize;
        size.y *= std::min((size_t) max_visible_rows, (size_t) std::max(visible_count, 1));
        size.y += visible_count > max_visible_rows ? rowSize.y / 2 : 0;
#ifdef __WXGTK__
        if (size.x < 1) size.x = 1;
        if (size.y < 1) size.y = 1;
#endif
        if (size != GetSize()) {
            wxWindow::SetSize(size);
            offset = wxPoint();
            Position(pos, off);
        }
    }
    if (GetPosition().y > pos.y) {
        // may exceed
        auto drect = wxDisplay(GetParent()).GetGeometry();
        if (GetPosition().y + size.y + 10 > drect.GetBottom()) {
            if (use_content_width && visible_count <= max_visible_rows)
                size.x += 6;
            size.y = drect.GetBottom() - GetPosition().y - 10;
#ifdef __WXGTK__
            if (size.y < 1) size.y = 1;
            if (size.x < 1) size.x = 1;
#endif
            wxWindow::SetSize(size);
            // Scroll by view row; the real index can exceed the folded row
            // count and over-scroll into blank space.
            int view_selection = selectedItem();
            if (view_selection >= 0) {
                if (offset.y + rowSize.y * (view_selection + 1) > size.y)
                    offset.y = size.y - rowSize.y * (view_selection + 1);
                else if (offset.y + rowSize.y * view_selection < 0)
                    offset.y = -rowSize.y * view_selection;
            }
            int min_offset = std::min(0, size.y - rowSize.y * std::max(visible_count, 0));
            offset.y       = std::clamp(offset.y, min_offset, 0);
        }
    }
    int display = wxDisplay::GetFromPoint(pos);
    if (display == wxNOT_FOUND)
        display = wxDisplay::GetFromWindow(GetParent());
    if (display != wxNOT_FOUND) {
        const wxRect work = wxDisplay(unsigned(display)).GetClientArea();
        size              = GetSize();
        size.x            = std::min(size.x, work.width);
        size.y            = std::min(size.y, work.height);
        wxPoint placed    = GetPosition();
        if (mainDropDown && pos.x + off.x + size.x > work.GetRight() + 1)
            placed.x = mainDropDown->GetScreenPosition().x - size.x + FromDIP(6);
        placed.x = std::clamp(placed.x, work.x, work.GetRight() + 1 - size.x);
        placed.y = std::clamp(placed.y, work.y, work.GetBottom() + 1 - size.y);
        SetSize(placed.x, placed.y, size.x, size.y);
        offset.y = clamp_dropdown_offset(offset.y, size.y, rowSize.y, visible_count);
    }
}

void DropDown::mouseDown(wxMouseEvent& event)
{
    // Receivce unexcepted LEFT_DOWN on Mac after OnDismiss
    if (!IsShown())
        return;
    // force calc hover item again
    mouseMove(event);
    pressedDown = true;
    CaptureMouse();
    dragStart   = event.GetPosition();
}

void DropDown::mouseReleased(wxMouseEvent& event)
{
    if (pressedDown) {
        dragStart = wxPoint();
        pressedDown = false;
        if (HasCapture())
            ReleaseMouse();
        if (hover_item < 0)
            return;

        // A group header opens the submenu instead of dismissing; hover alone
        // is flaky for one-row groups.
        int idx = hoverIndex();
        if (idx < -1 && subDropDown) {
            show_group(groups[-idx - 2]);
            return;
        }

        if (isSelectable(idx)) {
            // Close before dispatch: selection handlers may rebuild or destroy
            // the combo and its popup children synchronously.
            wxCommandEvent selected(wxEVT_COMBOBOX, GetId());
            selected.SetEventObject(this);
            selected.SetInt(idx);
            selected.SetString(texts[idx]);
            wxWeakRef<DropDown> alive(this);
            close_popup();
            if (alive && isSelectable(idx) && texts[idx] == selected.GetString())
                GetEventHandler()->ProcessEvent(selected);
            return;
        }
    }
}

void DropDown::mouseCaptureLost(wxMouseCaptureLostEvent &event)
{
    wxMouseEvent evt;
    mouseReleased(evt);
}

void DropDown::mouseMove(wxMouseEvent &event)
{
    wxPoint pt  = event.GetPosition();
#ifdef __WXOSX__
    if (mainDropDown) {
        auto size = GetSize();
        if (pt.x < 0 || pt.y < 0 || pt.x >= size.x || pt.y >= size.y) {
            auto diff = GetPosition() - mainDropDown->GetPosition();
            event.SetX(pt.x + diff.x);
            event.SetY(pt.y + diff.y);
            mainDropDown->mouseMove(event);
            return;
        }
    }
#endif
    if (pressedDown) {
        wxPoint pt2 = offset + pt - dragStart;
        wxSize  size = GetSize();
        dragStart    = pt;
        pt2.y        = clamp_dropdown_offset(pt2.y, size.y, rowSize.y, visible_count);
        if (pt2.y != offset.y) {
            offset = pt2;
            hover_item = -1; // moved
        } else {
            return;
        }
    }
    if (visible_count <= 0)
        return;
    if (!pressedDown || hover_item >= 0) {
        int hover = (pt.y - offset.y) / rowSize.y;
        if (hover >= visible_count)
            hover = -1;
        if (hover == hover_item) return;
        hover_item = hover;
        int index  = hoverIndex();
        clear_tip();
        if (index < -1) {
            show_group(groups[-index - 2]);
        } else if (index >= 0) {
            if (subDropDown) {
                subDropDown->group.clear();
                subDropDown->clear_tip();
                subDropDown->popup_active = false;
                if (subDropDown->IsShown())
                    subDropDown->Dismiss();
            }
        }
        update_tip();
    }
    paintNow();
}

void DropDown::mouseWheelMoved(wxMouseEvent &event)
{
    clear_tip();
    if (subDropDown) {
        subDropDown->clear_tip();
        subDropDown->popup_active = false;
        subDropDown->PopupWindow::Dismiss();
        subDropDown->group.clear();
    }
    auto delta = event.GetWheelRotation();
    wxSize  size  = GetSize();
    wxPoint pt2   = offset + wxPoint{0, delta};
    pt2.y         = clamp_dropdown_offset(pt2.y, size.y, rowSize.y, visible_count);
    if (pt2.y != offset.y) {
        offset = pt2;
    } else {
        return;
    }
    if (visible_count <= 0)
        return;
    int hover = (event.GetPosition().y - offset.y) / rowSize.y;
    if (hover >= visible_count)
        hover = -1;
    if (hover != hover_item) {
        hover_item = hover;
    }
    paintNow();
}

void DropDown::scroll_to_selection()
{
    int view = selectedItem();
    if (view < 0)
        return;
    wxSize size = GetSize();
    if (offset.y + rowSize.y * (view + 1) > size.y)
        offset.y = size.y - rowSize.y * (view + 1);
    else if (offset.y + rowSize.y * view < 0)
        offset.y = -rowSize.y * view;
    int min_offset = std::min(0, size.y - rowSize.y * std::max(visible_count, 0));
    offset.y       = std::clamp(offset.y, min_offset, 0);
    paintNow();
}

void DropDown::open_selected_group()
{
    int sel = GetSelection();
    if (sel < 0 || sel >= (int) groups.size())
        return;
    if (!hasGroups() || subDropDown == nullptr || groups[sel].IsEmpty()) {
        // Flat section: the selection is a visible top-level row.
        scroll_to_selection();
        return;
    }
    const wxString& target = groups[sel];
    // autoPosition() anchors the submenu to mainDropDown->hover_item; drive it
    // to the group's header row so a programmatic open aligns like a hover and
    // highlights the group holding the current selection.
    for (int row = 0; row < (int) m_view_to_real.size(); ++row) {
        int v = m_view_to_real[row];
        if (v < -1 && groups[-v - 2] == target) {
            hover_item = row;
            break;
        }
    }
    scroll_to_selection();
    show_group(target);
    subDropDown->scroll_to_selection();
}

void DropDown::show_group(const wxString& key)
{
    clear_tip();
    auto& drop = *subDropDown;
    drop.clear_tip();
    if (drop.group != key) {
        drop.group      = key;
        drop.offset     = wxPoint();
        drop.hover_item = -1;
        drop.need_sync  = true;
    }
    drop.selection = selection;
    drop.messureSize();
    drop.autoPosition();
    drop.paintNow();
    if (!drop.IsShown())
        drop.Popup(&drop);
}

void DropDown::clear_tip()
{
    UnsetToolTip();
    if (tip_window)
        tip_window->Hide();
}

void DropDown::update_tip()
{
    const int encoded = hoverIndex();
    if (encoded == -1 || !IsShown())
        return;
    const int  index = encoded < -1 ? -encoded - 2 : encoded;
    wxClientDC dc(this);
    dc.SetFont(GetFont());
    const wxString label     = displayed_text(index);
    const int      available = rowSize.x - iconSize.x - (check_bitmap.bmp().IsOk() ? check_bitmap.GetBmpWidth() : 0) - FromDIP(30) -
                          (encoded < -1 ? arrow_bitmap.GetBmpWidth() : 0);
    const bool     truncated = dc.GetTextExtent(label).x > available;
    const wxString text      = truncated ? (encoded < -1 ? label : texts[index]) : tips[index];
    if (text.empty())
        return;

    const wxPoint row_pos = ClientToScreen(wxPoint(0, offset.y + hover_item * rowSize.y));
    int           display = wxDisplay::GetFromPoint(row_pos);
    if (display == wxNOT_FOUND)
        display = wxDisplay::GetFromWindow(GetParent());
    if (display == wxNOT_FOUND)
        return;
    const wxRect work    = wxDisplay(unsigned(display)).GetClientArea();
    const int    padding = FromDIP(8);
    const int    width   = std::max(1, std::min(FromDIP(480), work.width - 2 * padding));
    wxString     wrapped, line;
    // Hard-wrap even a 512-character token without spaces.
    for (size_t i = 0; i < text.size(); ++i) {
        const wxString ch = text.substr(i, 1);
        if (ch == "\n" || (!line.empty() && dc.GetTextExtent(line + ch).x > width - 2 * padding)) {
            wrapped += line + "\n";
            line.clear();
        }
        if (ch != "\n")
            line += ch;
    }
    wrapped += line;
    if (!tip_window) {
        tip_window = new wxPopupWindow(this, wxBORDER_SIMPLE);
        tip_label  = new wxStaticText(tip_window, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxST_NO_AUTORESIZE);
    }
    tip_window->SetBackgroundColour(StateColor::darkModeColorFor(wxColour(255, 255, 225)));
    tip_label->SetForegroundColour(text_color.colorForStates(state_handler.states()));
    tip_label->SetFont(GetFont());
    tip_label->SetLabel(wrapped);
    wxSize extent = dc.GetMultiLineTextExtent(wrapped);
    wxSize size(std::min(width, extent.x + 2 * padding), std::min(work.height, extent.y + 2 * padding));
    int    x = std::clamp(row_pos.x, work.x, work.GetRight() + 1 - size.x);
    int    y = row_pos.y + rowSize.y + padding;
    if (y + size.y > work.GetBottom() + 1)
        y = row_pos.y - size.y - padding;
    // Never cover the hovered row if the available vertical space is too small.
    if (y < work.y)
        return;
    tip_label->SetSize(padding, padding, size.x - 2 * padding, size.y - 2 * padding);
    tip_window->SetSize(x, y, size.x, size.y);
    tip_window->Show();
}

void DropDown::Dismiss()
{
    if (subDropDown && subDropDown->IsShown())
        return;
    PopupWindow::Dismiss();
}

void DropDown::OnDismiss()
{
    clear_tip();
    if (!popup_active)
        return;
    if (mainDropDown) {
        popup_active             = false;
        const wxPoint& mouse_pos = wxGetMousePosition();
        if (!mainDropDown->GetScreenRect().Contains(mouse_pos))
            mainDropDown->DismissAndNotify();
#ifdef __WIN32__
        else
            SetActiveWindow(mainDropDown->GetHandle());
#endif
        return;
    }
    if (subDropDown && subDropDown->IsShown())
        return;
    popup_active = false;
    dismissTime = boost::posix_time::microsec_clock::universal_time();
    hover_item  = -1;
    clear_tip();
    wxCommandEvent e(EVT_DISMISS);
    GetEventHandler()->ProcessEvent(e);
}
