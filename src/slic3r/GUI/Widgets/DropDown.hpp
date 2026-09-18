#ifndef slic3r_GUI_DropDown_hpp_
#define slic3r_GUI_DropDown_hpp_

#include <boost/date_time/posix_time/posix_time.hpp>
#include <map>
#include <wx/stattext.h>
#include "../wxExtensions.hpp"
#include "StateHandler.hpp"
#include "PopupWindow.hpp"

#define DD_NO_CHECK_ICON    0x0001
#define DD_NO_TEXT          0x0002
#define DD_STYLE_MASK       0x0003

// Item style bits — a separate bit field from the window style bits above.
#define DD_ITEM_STYLE_SPLIT_ITEM 0x0001     // ----text----, text with horizontal line around it
#define DD_ITEM_STYLE_DISABLED 0x0002       // gray text, not selectable
#define DD_ITEM_STYLE_DIMMED 0x0004         // gray text, but still selectable
#define DD_ITEM_STYLE_NON_SELECTABLE 0x0008 // renders like a normal row, but keyboard/wheel/click cannot select it

wxDECLARE_EVENT(EVT_DISMISS, wxCommandEvent);

struct DDGroupMeta
{
    wxString label;
    bool     strip_prefix = false;
};

class DropDown : public PopupWindow
{
    std::vector<wxString> &       texts;
    std::vector<wxString> &       tips;
    std::vector<wxBitmap> &     icons;
    std::vector<wxString>&           groups; // group keys; identity only, never display text
    std::vector<int>&                item_styles;
    std::map<wxString, DDGroupMeta>& group_metas;
    bool                          need_sync  = false;
    int                              selection        = -1;    // real item index
    int                              hover_item       = -1;    // visible row index
    int                              visible_count    = -1;    // row count after group folding
    mutable bool                     m_groups_present = false; // refreshed by messureSize()
    // View<->real caches built once per messureSize(); header rows are encoded
    // as -i-2 in m_view_to_real, folded-away items map to -1 in m_real_to_view.
    std::vector<int>                     m_view_to_real;
    std::vector<int>                     m_real_to_view;
    std::vector<char>                    m_truncated;     // per-real flag, set by render()
    std::map<wxString, std::vector<int>> m_group_members; // group key -> member real indices, top level only
    wxString                             group;           // non-empty: this is a second-level list of that group
    DropDown*                            subDropDown{nullptr};
    DropDown*                            mainDropDown{nullptr};
    bool                                 popup_active = false;
    wxPopupWindow*                       tip_window   = nullptr;
    wxStaticText*                        tip_label    = nullptr;

    double radius = 0;
    bool   use_content_width = false;
    bool   limit_max_content_width = false;
    bool   align_icon        = false;
    bool   text_off          = false;

    wxSize textSize;
    wxSize iconSize;
    wxSize rowSize{100, 30};  // Initialize with default values

    StateHandler state_handler;
    StateColor   text_color;
    StateColor   border_color;
    StateColor   selector_border_color;
    StateColor   selector_background_color;
    ScalableBitmap check_bitmap;
    ScalableBitmap arrow_bitmap;
    int            max_visible_rows = 15;

    bool pressedDown = false;
    boost::posix_time::ptime dismissTime;
    wxPoint                  offset; // x not used
    wxPoint                  dragStart;

public:
    ~DropDown() override;
    void Popup(wxWindow* focus = nullptr) override;
    void close_popup();
    DropDown(std::vector<wxString>&           texts,
             std::vector<wxString>&           tips,
             std::vector<wxBitmap>&           icons,
             std::vector<wxString>&           groups,
             std::vector<int>&                item_styles,
             std::map<wxString, DDGroupMeta>& group_metas);

    DropDown(wxWindow*                        parent,
             std::vector<wxString>&           texts,
             std::vector<wxString>&           tips,
             std::vector<wxBitmap>&           icons,
             std::vector<wxString>&           groups,
             std::vector<int>&                item_styles,
             std::map<wxString, DDGroupMeta>& group_metas,
             long                             style = 0);

    void Create(wxWindow *     parent,
             long           style     = 0);

public:
    void Invalidate(bool clear = false);

    int GetSelection() const { return selection; }

    void SetSelection(int n);

    wxString GetValue() const;
    void     SetValue(const wxString &value);

public:
    void SetCornerRadius(double radius);

    void SetBorderColor(StateColor const & color);

    void SetSelectorBorderColor(StateColor const & color);

    void SetTextColor(StateColor const &color);

    void SetSelectorBackgroundColor(StateColor const &color);

    void SetUseContentWidth(bool use, bool limit_max_content_width = false);

    void SetAlignIcon(bool align);

public:
    void Rescale();

    bool HasDismissLongTime();

    // Popup helper for "current selection visible on open": opens the group
    // holding the current selection and scrolls to it, without emitting any
    // selection event or extra closeup.
    void open_selected_group();

protected:
    // Keeps the main popup visible while the submenu is shown; OnDismiss()
    // runs after hiding — too late to prevent it.
    void Dismiss() override;

    void OnDismiss() override;

private:
    void paintEvent(wxPaintEvent& evt);
    void paintNow();

    void render(wxDC& dc);

    wxString displayed_text(size_t i) const;

    // Visible row -> real item index; group header rows are encoded as -i-2.
    int  hoverIndex();
    int  selectedItem();
    bool hasGroups() const;

    // Selectability gate shared by mouse, keyboard, wheel and programmatic paths.
    bool isSelectable(int real_index) const;

    friend class ComboBox;
    void messureSize();
    void autoPosition();
    void scroll_to_selection();
    void show_group(const wxString& key);
    void clear_tip();
    void update_tip();

    // some useful events
    void mouseDown(wxMouseEvent& event);
    void mouseReleased(wxMouseEvent &event);
    void mouseCaptureLost(wxMouseCaptureLostEvent &event);
    void mouseMove(wxMouseEvent &event);
    void mouseWheelMoved(wxMouseEvent &event);


    DECLARE_EVENT_TABLE()
};

#endif // !slic3r_GUI_DropDown_hpp_
