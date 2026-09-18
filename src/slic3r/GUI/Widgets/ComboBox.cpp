#include "ComboBox.hpp"
#include "Label.hpp"

#include <wx/dcgraph.h>
#include <wx/weakref.h>

BEGIN_EVENT_TABLE(ComboBox, TextInput)

EVT_LEFT_DOWN(ComboBox::mouseDown)
EVT_LEFT_DCLICK(ComboBox::mouseDown)
//EVT_MOUSEWHEEL(ComboBox::mouseWheelMoved)
EVT_KEY_DOWN(ComboBox::keyDown)

// catch paint events
END_EVENT_TABLE()

/*
 * Called by the system of by wxWidgets when the panel needs
 * to be redrawn. You can also trigger this call by
 * calling Refresh()/Update().
 */

static wxWindow *GetScrollParent(wxWindow *pWindow)
{
    wxWindow *pWin = pWindow;
    while (pWin->GetParent()) {
        auto pWin2 = pWin->GetParent();
        if (auto top = dynamic_cast<wxScrollHelper *>(pWin2))
            return dynamic_cast<wxWindow *>(pWin);
        pWin = pWin2;
    }
    return nullptr;
}

ComboBox::ComboBox(wxWindow*       parent,
                   wxWindowID      id,
                   const wxString& value,
                   const wxPoint&  pos,
                   const wxSize&   size,
                   int             n,
                   const wxString  choices[],
                   long            style)
    : drop(texts, tips, icons, groups, item_styles, group_metas)
{
    if (style & wxCB_READONLY)
        style |= wxRIGHT;
    text_off = style & CB_NO_TEXT;
    TextInput::Create(parent, "", value, (style & CB_NO_DROP_ICON) ? "" : "drop_down", pos, size,
                      style | wxTE_PROCESS_ENTER);
    drop.Create(this, style & DD_STYLE_MASK);

    if (style & wxCB_READONLY) {
        GetTextCtrl()->Hide();
        TextInput::SetFont(Label::Body_14);
        TextInput::SetBorderColor(StateColor(std::make_pair(0xDBDBDB, (int) StateColor::Disabled),
            std::make_pair(0x009688, (int) StateColor::Hovered),
            std::make_pair(0xDBDBDB, (int) StateColor::Normal)));
        TextInput::SetBackgroundColor(StateColor(std::make_pair(0xF0F0F1, (int) StateColor::Disabled),
            std::make_pair(0xE5F0EE, (int) StateColor::Focused), // ORCA updated background color for focused item
            std::make_pair(*wxWHITE, (int) StateColor::Normal)));
        TextInput::SetLabelColor(StateColor(
            std::make_pair(0x6B6B6B, (int) StateColor::Disabled), // ORCA: Use same color for disabled text on combo boxes
            std::make_pair(0x262E30, (int) StateColor::Normal)));
    }
    if (auto scroll = GetScrollParent(this))
        scroll->Bind(wxEVT_MOVE, &ComboBox::onMove, this);
    drop.Bind(wxEVT_COMBOBOX, [this](wxCommandEvent &e) {
        SetSelection(e.GetInt());
        e.SetEventObject(this);
        e.SetId(GetId());
        GetEventHandler()->ProcessEvent(e);
    });
    drop.Bind(EVT_DISMISS, [this](auto&) {
        if (!drop_down)
            return;
        drop_down = false;
        wxCommandEvent e(wxEVT_COMBOBOX_CLOSEUP);
        GetEventHandler()->ProcessEvent(e);
    });
    for (int i = 0; i < n; ++i) Append(choices[i]);
}

int ComboBox::GetSelection() const { return drop.GetSelection(); }

void ComboBox::SetSelection(int n)
{
    if (n >= 0 && !drop.isSelectable(n))
        return;
    if (n == drop.selection)
        return;
    drop.SetSelection(n);
    SetLabel(drop.GetValue());
    if (drop.selection >= 0 && drop.iconSize.y > 0)
        SetIcon(icons[drop.selection].IsNull() ? create_scaled_bitmap("drop_down", nullptr, 16): icons[drop.selection]); // ORCA fix combo boxes without arrows
}
void ComboBox::SelectAndNotify(int n)
{
    if (!drop.isSelectable(n))
        return;
    SetSelection(n);
    sendComboBoxEvent();
}

void ComboBox::SetItemStyle(unsigned int n, int style)
{
    if (n < item_styles.size()) {
        item_styles[n] = style;
        drop.Invalidate();
    }
}

void ComboBox::Rescale()
{
    TextInput::Rescale();
    drop.Rescale();
}

wxString ComboBox::GetValue() const
{
    return drop.GetSelection() >= 0 ? drop.GetValue() : GetLabel();
}

void ComboBox::SetValue(const wxString &value)
{
    drop.SetValue(value);
    SetLabel(value);
    if (drop.selection >= 0 && drop.iconSize.y > 0)
        SetIcon(icons[drop.selection].IsNull() ? create_scaled_bitmap("drop_down", nullptr, 16): icons[drop.selection]); // ORCA fix combo boxes without arrows
}

void ComboBox::SetLabel(const wxString &value)
{
    if (GetTextCtrl()->IsShown() || text_off)
        GetTextCtrl()->SetValue(value);
    else
        TextInput::SetLabel(value);
}

wxString ComboBox::GetLabel() const
{
    if (GetTextCtrl()->IsShown() || text_off)
        return GetTextCtrl()->GetValue();
    else
        return TextInput::GetLabel();
}

void ComboBox::SetTextLabel(const wxString& label)
{
    TextInput::SetLabel(label);
}

wxString ComboBox::GetTextLabel() const
{
    return TextInput::GetLabel();
}

bool ComboBox::SetFont(wxFont const& font)
{
    if (GetTextCtrl() && GetTextCtrl()->IsShown())
        return GetTextCtrl()->SetFont(font);
    else
        return TextInput::SetFont(font);
}

void ComboBox::assert_parallel_arrays() const
{
    wxASSERT(texts.size() == tips.size());
    wxASSERT(texts.size() == icons.size());
    wxASSERT(texts.size() == groups.size());
    wxASSERT(texts.size() == item_styles.size());
    wxASSERT(texts.size() == datas.size());
    wxASSERT(texts.size() == types.size());
}

int ComboBox::Append(const wxString &item, const wxBitmap &bitmap)
{
    return Append(item, bitmap, nullptr);
}

int ComboBox::Append(const wxString &item,
                     const wxBitmap &bitmap,
                     void *          clientData)
{
    return Append(item, bitmap, wxString{}, clientData, 0);
}

int ComboBox::Append(const wxString& item, const wxBitmap& bitmap, const wxString& group, void* clientData, int item_style)
{
    if (!group.empty())
        group_metas.emplace(group, DDGroupMeta{group, false});
    return Append(item, bitmap, group, DDGroupMeta{}, clientData, item_style);
}

int ComboBox::Append(
    const wxString& item, const wxBitmap& bitmap, const wxString& group_key, const DDGroupMeta& meta, void* clientData, int item_style)
{
    if (!group_key.empty())
        group_metas.emplace(group_key, meta);
    texts.push_back(item);
    tips.push_back(wxString{});
    icons.push_back(bitmap);
    groups.push_back(group_key);
    item_styles.push_back(item_style);
    datas.push_back(clientData);
    types.push_back(wxClientData_None);
    drop.Invalidate();
    assert_parallel_arrays();
    return texts.size() - 1;
}

void ComboBox::DoClear()
{
    close_popup();
    SetIcon("drop_down");
    texts.clear();
    tips.clear();
    icons.clear();
    groups.clear();
    item_styles.clear();
    group_metas.clear();
    datas.clear();
    types.clear();
    drop.Invalidate(true);
    assert_parallel_arrays();
}

void ComboBox::DoDeleteOneItem(unsigned int pos)
{
    close_popup();
    if (pos >= texts.size()) return;
    texts.erase(texts.begin() + pos);
    tips.erase(tips.begin() + pos);
    icons.erase(icons.begin() + pos);
    groups.erase(groups.begin() + pos);
    item_styles.erase(item_styles.begin() + pos);
    datas.erase(datas.begin() + pos);
    types.erase(types.begin() + pos);
    drop.Invalidate(true);
    assert_parallel_arrays();
}

unsigned int ComboBox::GetCount() const { return texts.size(); }

wxString ComboBox::GetString(unsigned int n) const
{
    return n < texts.size() ? texts[n] : wxString{};
}

void ComboBox::SetString(unsigned int n, wxString const &value)
{
    if (n >= texts.size()) return;
    texts[n]  = value;
    drop.Invalidate();
    if (n == drop.GetSelection()) SetLabel(value);
}

wxString ComboBox::GetItemTooltip(unsigned int n) const
{
    if (n >= texts.size()) return wxString();
    return tips[n];
}

void ComboBox::SetItemTooltip(unsigned int n, wxString const &value) {
    if (n >= texts.size()) return;
    tips[n] = value;
    drop.clear_tip();
}

wxBitmap ComboBox::GetItemBitmap(unsigned int n) { return icons[n]; }

void ComboBox::SetItemBitmap(unsigned int n, wxBitmap const &bitmap)
{
    if (n >= texts.size()) return;
    icons[n] = bitmap;
    drop.Invalidate();
}

int ComboBox::DoInsertItems(const wxArrayStringsAdapter &items,
                            unsigned int                 pos,
                            void **                      clientData,
                            wxClientDataType             type)
{
    if (pos > texts.size()) return -1;
    for (int i = 0; i < items.GetCount(); ++i) {
        texts.insert(texts.begin() + pos, items[i]);
        tips.insert(tips.begin() + pos, wxString{});
        icons.insert(icons.begin() + pos, wxNullBitmap);
        groups.insert(groups.begin() + pos, wxString{});
        item_styles.insert(item_styles.begin() + pos, 0);
        datas.insert(datas.begin() + pos, clientData ? clientData[i] : NULL);
        types.insert(types.begin() + pos, type);
        ++pos;
    }
    drop.Invalidate(true);
    assert_parallel_arrays();
    return pos - 1;
}

void *ComboBox::DoGetItemClientData(unsigned int n) const { return n < texts.size() ? datas[n] : NULL; }

void ComboBox::DoSetItemClientData(unsigned int n, void *data)
{
    if (n < texts.size())
        datas[n] = data;
}

void ComboBox::close_popup() { drop.close_popup(); }

void ComboBox::mouseDown(wxMouseEvent &event)
{
    SetFocus();
    if (drop_down) {
        drop.Hide();
        drop_down = false;
    } else if (drop.HasDismissLongTime()) {
        drop.autoPosition();
        drop_down = true;
        drop.Popup(&drop);
        if (open_selected_group_on_popup) {
            wxWeakRef<ComboBox> self(this);
            CallAfter([self]() {
                if (self && self->drop_down)
                    self->drop.open_selected_group();
            });
        }
        wxCommandEvent e(wxEVT_COMBOBOX_DROPDOWN);
        GetEventHandler()->ProcessEvent(e);
    }
}

void ComboBox::mouseWheelMoved(wxMouseEvent &event)
{
    event.Skip();
    if (drop_down) return;
    auto delta = event.GetWheelRotation() < 0 ? 1 : -1;
    int  n     = GetSelection();
    do {
        n += delta;
    } while (n >= 0 && n < (int) texts.size() && !drop.isSelectable(n));
    if (n < 0 || n >= (int) texts.size() || !drop.isSelectable(n))
        return;
    SetSelection(n);
    sendComboBoxEvent();
}

void ComboBox::keyDown(wxKeyEvent& event)
{
    switch (event.GetKeyCode()) {
        case WXK_RETURN:
        case WXK_SPACE:
            if (drop_down) {
                drop.DismissAndNotify();
            } else if (drop.HasDismissLongTime()) {
                drop.autoPosition();
                drop_down = true;
                drop.Popup();
                if (open_selected_group_on_popup) {
                    wxWeakRef<ComboBox> self(this);
                    CallAfter([self]() {
                        if (self && self->drop_down)
                            self->drop.open_selected_group();
                    });
                }
                wxCommandEvent e(wxEVT_COMBOBOX_DROPDOWN);
                GetEventHandler()->ProcessEvent(e);
            }
            break;
        case WXK_ESCAPE:
            if (drop_down)
                close_popup();
            else
                event.Skip();
            break;
        case WXK_UP:
        case WXK_DOWN:
        case WXK_LEFT:
        case WXK_RIGHT: {
            if (drop_down) {
                // Arrows must not select folded-away members while the list is open.
                break;
            }
            bool up = event.GetKeyCode() == WXK_UP || event.GetKeyCode() == WXK_LEFT;
            int  n  = GetSelection();
            do {
                n += up ? -1 : 1;
            } while (n >= 0 && n < (int) texts.size() && !drop.isSelectable(n));
            if (n < 0 || n >= (int) texts.size() || !drop.isSelectable(n))
                break;
            SetSelection(n);
            sendComboBoxEvent();
            break;
        }
        case WXK_TAB:
            HandleAsNavigationKey(event);
            break;
        default:
            event.Skip();
            break;
    }
}

void ComboBox::onMove(wxMoveEvent &event)
{
    event.Skip();
    close_popup();
}

void ComboBox::OnEdit()
{
    auto value = GetTextCtrl()->GetValue();
    SetValue(value);
}

#ifdef __WIN32__

WXLRESULT ComboBox::MSWWindowProc(WXUINT nMsg, WXWPARAM wParam, WXLPARAM lParam)
{
    if (nMsg == WM_GETDLGCODE) {
        return DLGC_WANTALLKEYS;
    }
    return TextInput::MSWWindowProc(nMsg, wParam, lParam);
}

#endif

void ComboBox::sendComboBoxEvent()
{
    wxCommandEvent event(wxEVT_COMBOBOX, GetId());
    event.SetEventObject(this);
    event.SetInt(drop.GetSelection());
    event.SetString(drop.GetValue());
    GetEventHandler()->ProcessEvent(event);
}
