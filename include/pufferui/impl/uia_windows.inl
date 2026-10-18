// pufferui/impl/uia_windows.inl - part of the PufferUI implementation (see pufferui.h).
// The Windows UI Automation adapter (screen readers: Narrator, NVDA, JAWS) for
// SDL3 windows. Included at the very end of the implementation block, after
// every other slice, so the Windows headers' macros cannot reach the rest of
// the library; never include it directly.
//
// The window's HWND is subclassed (SetWindowSubclass) to answer WM_GETOBJECT
// with a fragment root; its children are the nodes the window's last frame
// published (ui::a11y). Elements are addressed by widget id and looked up in
// the current snapshot on every call, so an element whose widget disappeared
// reports UIA_E_ELEMENTNOTAVAILABLE instead of stale data. UIA calls arrive on
// the thread that pumps the window's messages (between frames).
namespace pui
{
namespace
{

std::wstring uia_wide(std::string_view s)
{
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<usize>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

LONG uia_control_type(a11y_role r)
{
    switch (r)
    {
    case a11y_role::BUTTON:
        return UIA_ButtonControlTypeId;
    case a11y_role::CHECKBOX:
    case a11y_role::SWITCH:
        return UIA_CheckBoxControlTypeId;
    case a11y_role::RADIO:
        return UIA_RadioButtonControlTypeId;
    case a11y_role::TEXT_FIELD:
    case a11y_role::TEXT_AREA:
        return UIA_EditControlTypeId;
    case a11y_role::SLIDER:
        return UIA_SliderControlTypeId;
    case a11y_role::COMBO:
        return UIA_ComboBoxControlTypeId;
    case a11y_role::LIST_ITEM:
        return UIA_ListItemControlTypeId;
    case a11y_role::TAB:
        return UIA_TabItemControlTypeId;
    case a11y_role::TEXT:
        return UIA_TextControlTypeId;
    case a11y_role::GROUP:
        return UIA_GroupControlTypeId;
    default:
        return UIA_CustomControlTypeId;
    }
}

bool uia_invokable(a11y_role r)
{
    return r == a11y_role::BUTTON || r == a11y_role::LIST_ITEM || r == a11y_role::TAB ||
           r == a11y_role::RADIO;
}
bool uia_toggleable(a11y_role r)
{
    return r == a11y_role::CHECKBOX || r == a11y_role::SWITCH;
}
bool uia_valued(a11y_role r)
{
    return r == a11y_role::TEXT_FIELD || r == a11y_role::TEXT_AREA || r == a11y_role::SLIDER ||
           r == a11y_role::COMBO;
}

struct uia_root;

// One widget of the window, by id.
struct uia_node final : IRawElementProviderSimple,
                        IRawElementProviderFragment,
                        IInvokeProvider,
                        IToggleProvider,
                        IValueProvider
{
    LONG refs = 1;
    uia_root *root = nullptr;
    uiid id = 0;

    uia_node(uia_root *r, uiid i);
    ~uia_node();
    const a11y_tree::node *find(i32 *index = nullptr) const;

    // IUnknown
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&refs); }
    ULONG STDMETHODCALLTYPE Release() override
    {
        const LONG n = InterlockedDecrement(&refs);
        if (n == 0) delete this;
        return static_cast<ULONG>(n);
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **out) override
    {
        if (!out) return E_INVALIDARG;
        *out = nullptr;
        if (riid == __uuidof(IUnknown) || riid == __uuidof(IRawElementProviderSimple))
            *out = static_cast<IRawElementProviderSimple *>(this);
        else if (riid == __uuidof(IRawElementProviderFragment))
            *out = static_cast<IRawElementProviderFragment *>(this);
        else if (riid == __uuidof(IInvokeProvider))
            *out = static_cast<IInvokeProvider *>(this);
        else if (riid == __uuidof(IToggleProvider))
            *out = static_cast<IToggleProvider *>(this);
        else if (riid == __uuidof(IValueProvider))
            *out = static_cast<IValueProvider *>(this);
        else
            return E_NOINTERFACE;
        AddRef();
        return S_OK;
    }

    // IRawElementProviderSimple
    HRESULT STDMETHODCALLTYPE get_ProviderOptions(ProviderOptions *o) override
    {
        if (!o) return E_INVALIDARG;
        // COM threading: UIA marshals every call onto the thread that pumps the
        // window, so the frame data is never read or written concurrently.
        *o = static_cast<ProviderOptions>(ProviderOptions_ServerSideProvider |
                                          ProviderOptions_UseComThreading);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetPatternProvider(PATTERNID pattern, IUnknown **out) override
    {
        if (!out) return E_INVALIDARG;
        *out = nullptr;
        const a11y_tree::node *n = find();
        if (!n) return UIA_E_ELEMENTNOTAVAILABLE;
        if ((pattern == UIA_InvokePatternId && uia_invokable(n->role)) ||
            (pattern == UIA_TogglePatternId && uia_toggleable(n->role)) ||
            (pattern == UIA_ValuePatternId && uia_valued(n->role)))
        {
            *out = static_cast<IRawElementProviderSimple *>(this);
            AddRef();
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetPropertyValue(PROPERTYID prop, VARIANT *v) override;
    HRESULT STDMETHODCALLTYPE get_HostRawElementProvider(IRawElementProviderSimple **out) override
    {
        if (!out) return E_INVALIDARG;
        *out = nullptr;
        return S_OK;
    }

    // IRawElementProviderFragment
    HRESULT STDMETHODCALLTYPE Navigate(NavigateDirection dir,
                                       IRawElementProviderFragment **out) override;
    HRESULT STDMETHODCALLTYPE GetRuntimeId(SAFEARRAY **out) override
    {
        if (!out) return E_INVALIDARG;
        *out = SafeArrayCreateVector(VT_I4, 0, 3);
        if (!*out) return E_OUTOFMEMORY;
        LONG i = 0;
        int part = UiaAppendRuntimeId;
        SafeArrayPutElement(*out, &i, &part);
        i = 1;
        part = static_cast<int>(id & 0x7FFFFFFFull);
        SafeArrayPutElement(*out, &i, &part);
        i = 2;
        part = static_cast<int>((id >> 32) & 0x7FFFFFFFull);
        SafeArrayPutElement(*out, &i, &part);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_BoundingRectangle(UiaRect *out) override;
    HRESULT STDMETHODCALLTYPE GetEmbeddedFragmentRoots(SAFEARRAY **out) override
    {
        if (out) *out = nullptr;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetFocus() override;
    HRESULT STDMETHODCALLTYPE get_FragmentRoot(IRawElementProviderFragmentRoot **out) override;

    // IInvokeProvider / IToggleProvider: a click in the window's next frame.
    HRESULT STDMETHODCALLTYPE Invoke() override;
    HRESULT STDMETHODCALLTYPE Toggle() override { return Invoke(); }
    HRESULT STDMETHODCALLTYPE get_ToggleState(ToggleState *s) override
    {
        if (!s) return E_INVALIDARG;
        const a11y_tree::node *n = find();
        if (!n) return UIA_E_ELEMENTNOTAVAILABLE;
        *s = (n->state & A11Y_CHECKED) ? ToggleState_On : ToggleState_Off;
        return S_OK;
    }

    // IValueProvider: read-only (the app's model owns the text).
    HRESULT STDMETHODCALLTYPE SetValue(LPCWSTR) override { return UIA_E_NOTSUPPORTED; }
    HRESULT STDMETHODCALLTYPE get_Value(BSTR *out) override
    {
        if (!out) return E_INVALIDARG;
        const a11y_tree::node *n = find();
        if (!n) return UIA_E_ELEMENTNOTAVAILABLE;
        *out = SysAllocString(uia_wide(n->value).c_str());
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_IsReadOnly(BOOL *out) override
    {
        if (!out) return E_INVALIDARG;
        *out = TRUE;
        return S_OK;
    }
};

// The window: the fragment root, hosted by the HWND's own provider.
struct uia_root final : IRawElementProviderSimple,
                        IRawElementProviderFragment,
                        IRawElementProviderFragmentRoot
{
    LONG refs = 1;
    window *w = nullptr; // null once the core window is gone
    HWND hwnd = nullptr;
    uiid last_focus = 0;

    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&refs); }
    ULONG STDMETHODCALLTYPE Release() override
    {
        const LONG n = InterlockedDecrement(&refs);
        if (n == 0) delete this;
        return static_cast<ULONG>(n);
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **out) override
    {
        if (!out) return E_INVALIDARG;
        *out = nullptr;
        if (riid == __uuidof(IUnknown) || riid == __uuidof(IRawElementProviderSimple))
            *out = static_cast<IRawElementProviderSimple *>(this);
        else if (riid == __uuidof(IRawElementProviderFragment))
            *out = static_cast<IRawElementProviderFragment *>(this);
        else if (riid == __uuidof(IRawElementProviderFragmentRoot))
            *out = static_cast<IRawElementProviderFragmentRoot *>(this);
        else
            return E_NOINTERFACE;
        AddRef();
        return S_OK;
    }

    i32 count() const { return w && w->a11y ? static_cast<i32>(w->a11y->nodes.size()) : 0; }
    IRawElementProviderFragment *child(i32 i)
    {
        if (i < 0 || i >= count()) return nullptr;
        return new uia_node(this, w->a11y->nodes[static_cast<usize>(i)].id);
    }

    // Window-local logical units -> screen pixels.
    UiaRect to_screen(rect r) const
    {
        UiaRect out{};
        if (!w || !hwnd) return out;
        POINT origin{0, 0};
        ClientToScreen(hwnd, &origin);
        RECT client{};
        GetClientRect(hwnd, &client);
        const f64 s = w->area.w > 0.0f ? static_cast<f64>(client.right) / w->area.w : 1.0;
        out.left = origin.x + r.x * s;
        out.top = origin.y + r.y * s;
        out.width = r.w * s;
        out.height = r.h * s;
        return out;
    }

    // IRawElementProviderSimple
    HRESULT STDMETHODCALLTYPE get_ProviderOptions(ProviderOptions *o) override
    {
        if (!o) return E_INVALIDARG;
        // COM threading: UIA marshals every call onto the thread that pumps the
        // window, so the frame data is never read or written concurrently.
        *o = static_cast<ProviderOptions>(ProviderOptions_ServerSideProvider |
                                          ProviderOptions_UseComThreading);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetPatternProvider(PATTERNID, IUnknown **out) override
    {
        if (out) *out = nullptr;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetPropertyValue(PROPERTYID prop, VARIANT *v) override
    {
        if (!v) return E_INVALIDARG;
        VariantInit(v);
        if (prop == UIA_FrameworkIdPropertyId)
        {
            v->vt = VT_BSTR;
            v->bstrVal = SysAllocString(L"PufferUI");
        }
        return S_OK; // the rest comes from the HWND's host provider
    }
    HRESULT STDMETHODCALLTYPE get_HostRawElementProvider(IRawElementProviderSimple **out) override
    {
        if (!out) return E_INVALIDARG;
        return UiaHostProviderFromHwnd(hwnd, out);
    }

    // IRawElementProviderFragment
    HRESULT STDMETHODCALLTYPE Navigate(NavigateDirection dir,
                                       IRawElementProviderFragment **out) override
    {
        if (!out) return E_INVALIDARG;
        *out = nullptr;
        if (dir == NavigateDirection_FirstChild) *out = child(0);
        if (dir == NavigateDirection_LastChild) *out = child(count() - 1);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetRuntimeId(SAFEARRAY **out) override
    {
        if (out) *out = nullptr; // the host provider supplies it
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_BoundingRectangle(UiaRect *out) override
    {
        if (!out) return E_INVALIDARG;
        *out = UiaRect{}; // the host provider supplies it
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetEmbeddedFragmentRoots(SAFEARRAY **out) override
    {
        if (out) *out = nullptr;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetFocus() override { return S_OK; }
    HRESULT STDMETHODCALLTYPE get_FragmentRoot(IRawElementProviderFragmentRoot **out) override
    {
        if (!out) return E_INVALIDARG;
        *out = this;
        AddRef();
        return S_OK;
    }

    // IRawElementProviderFragmentRoot
    HRESULT STDMETHODCALLTYPE ElementProviderFromPoint(double x, double y,
                                                       IRawElementProviderFragment **out) override
    {
        if (!out) return E_INVALIDARG;
        *out = nullptr;
        // The topmost (last submitted) node under the point.
        for (i32 i = count() - 1; i >= 0; --i)
        {
            const UiaRect r = to_screen(w->a11y->nodes[static_cast<usize>(i)].bounds);
            if (x >= r.left && x < r.left + r.width && y >= r.top && y < r.top + r.height)
            {
                *out = child(i);
                return S_OK;
            }
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetFocus(IRawElementProviderFragment **out) override
    {
        if (!out) return E_INVALIDARG;
        *out = nullptr;
        for (i32 i = 0; i < count(); ++i)
            if (w->a11y->nodes[static_cast<usize>(i)].state & A11Y_FOCUSED)
            {
                *out = child(i);
                break;
            }
        return S_OK;
    }
};

uia_node::uia_node(uia_root *r, uiid i) : root(r), id(i)
{
    root->AddRef();
}
uia_node::~uia_node()
{
    root->Release();
}

const a11y_tree::node *uia_node::find(i32 *index) const
{
    if (!root->w || !root->w->a11y) return nullptr;
    const std::vector<a11y_tree::node> &nodes = root->w->a11y->nodes;
    for (usize i = 0; i < nodes.size(); ++i)
        if (nodes[i].id == id)
        {
            if (index) *index = static_cast<i32>(i);
            return &nodes[i];
        }
    return nullptr;
}

HRESULT uia_node::GetPropertyValue(PROPERTYID prop, VARIANT *v)
{
    if (!v) return E_INVALIDARG;
    VariantInit(v);
    const a11y_tree::node *n = find();
    if (!n) return UIA_E_ELEMENTNOTAVAILABLE;
    const auto set_bool = [&](bool b)
    {
        v->vt = VT_BOOL;
        v->boolVal = b ? VARIANT_TRUE : VARIANT_FALSE;
    };
    const auto set_str = [&](std::string_view s)
    {
        v->vt = VT_BSTR;
        v->bstrVal = SysAllocString(uia_wide(s).c_str());
    };
    switch (prop)
    {
    case UIA_NamePropertyId:
        set_str(n->name);
        break;
    case UIA_ControlTypePropertyId:
        v->vt = VT_I4;
        v->lVal = uia_control_type(n->role);
        break;
    case UIA_AutomationIdPropertyId:
    {
        char buf[24];
        std::snprintf(buf, sizeof(buf), "%016llx", static_cast<unsigned long long>(n->id));
        set_str(buf);
        break;
    }
    case UIA_FrameworkIdPropertyId:
        set_str("PufferUI");
        break;
    case UIA_IsEnabledPropertyId:
        set_bool(!(n->state & A11Y_DISABLED));
        break;
    case UIA_HasKeyboardFocusPropertyId:
        set_bool((n->state & A11Y_FOCUSED) != 0);
        break;
    case UIA_IsKeyboardFocusablePropertyId:
    case UIA_IsControlElementPropertyId:
    case UIA_IsContentElementPropertyId:
        set_bool(true);
        break;
    case UIA_IsOffscreenPropertyId:
        set_bool(false);
        break;
    default:
        break; // VT_EMPTY: UIA uses its defaults
    }
    return S_OK;
}

HRESULT uia_node::Navigate(NavigateDirection dir, IRawElementProviderFragment **out)
{
    if (!out) return E_INVALIDARG;
    *out = nullptr;
    i32 index = -1;
    if (!find(&index)) return UIA_E_ELEMENTNOTAVAILABLE;
    if (dir == NavigateDirection_Parent)
    {
        *out = root;
        root->AddRef();
    }
    else if (dir == NavigateDirection_NextSibling)
        *out = root->child(index + 1);
    else if (dir == NavigateDirection_PreviousSibling)
        *out = root->child(index - 1);
    return S_OK; // a flat tree: no children
}

HRESULT uia_node::get_BoundingRectangle(UiaRect *out)
{
    if (!out) return E_INVALIDARG;
    const a11y_tree::node *n = find();
    if (!n) return UIA_E_ELEMENTNOTAVAILABLE;
    *out = root->to_screen(n->bounds);
    return S_OK;
}

HRESULT uia_node::SetFocus()
{
    if (!find() || !root->w) return UIA_E_ELEMENTNOTAVAILABLE;
    a11y_focus(*root->w, id);
    return S_OK;
}

HRESULT uia_node::get_FragmentRoot(IRawElementProviderFragmentRoot **out)
{
    if (!out) return E_INVALIDARG;
    *out = root;
    root->AddRef();
    return S_OK;
}

HRESULT uia_node::Invoke()
{
    if (!find() || !root->w) return UIA_E_ELEMENTNOTAVAILABLE;
    if (find()->state & A11Y_DISABLED) return UIA_E_ELEMENTNOTENABLED;
    a11y_invoke(*root->w, id);
    return S_OK;
}

// After a frame published its nodes: announce a keyboard focus change.
void uia_published(window &w)
{
    uia_root *root = static_cast<uia_root *>(w.a11y_user);
    if (!root || !w.a11y) return;
    uiid focus = 0;
    for (const a11y_tree::node &n : w.a11y->nodes)
        if (n.state & A11Y_FOCUSED) focus = n.id;
    if (focus == root->last_focus) return;
    root->last_focus = focus;
    if (focus == 0 || !UiaClientsAreListening()) return;
    uia_node *node = new uia_node(root, focus);
    UiaRaiseAutomationEvent(node, UIA_AutomationFocusChangedEventId);
    node->Release();
}

void uia_detach(window &w)
{
    if (uia_root *root = static_cast<uia_root *>(w.a11y_user)) root->w = nullptr;
}

LRESULT CALLBACK uia_subclass(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR id,
                              DWORD_PTR data)
{
    uia_root *root = reinterpret_cast<uia_root *>(data);
    if (msg == WM_GETOBJECT && static_cast<long>(lp) == static_cast<long>(UiaRootObjectId) && root)
    {
        // The first query from a client turns recording on (zero cost until a
        // screen reader or another UIA client is actually looking).
        if (root->w && root->w->owner && !root->w->owner->a11y_enabled)
        {
            set_accessibility(root->w->owner, true);
            root->w->owner->redraw_requested = true;
        }
        return UiaReturnRawElementProvider(hwnd, wp, lp, root);
    }
    if (msg == WM_NCDESTROY)
    {
        RemoveWindowSubclass(hwnd, uia_subclass, id);
        UiaReturnRawElementProvider(hwnd, 0, 0, nullptr); // UIA drops its references
        if (root)
        {
            if (root->w && root->w->a11y_user == root)
            {
                root->w->on_a11y = nullptr;
                root->w->on_a11y_detach = nullptr;
                root->w->a11y_user = nullptr;
            }
            root->w = nullptr;
            root->Release();
        }
    }
    return DefSubclassProc(hwnd, msg, wp, lp);
}
} // namespace

bool sdl3_enable_uia(window &w)
{
    SDL_Window *sdl = static_cast<SDL_Window *>(w.handle);
    if (!sdl || !w.owner) return false;
    if (w.a11y_user) return true; // already on
    HWND hwnd = static_cast<HWND>(SDL_GetPointerProperty(
        SDL_GetWindowProperties(sdl), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
    if (!hwnd) return false;
    uia_root *root = new uia_root();
    root->w = &w;
    root->hwnd = hwnd;
    if (!SetWindowSubclass(hwnd, uia_subclass, 0x50554955 /* 'PUIU' */,
                           reinterpret_cast<DWORD_PTR>(root)))
    {
        root->Release();
        return false;
    }
    w.a11y_user = root;
    w.on_a11y = uia_published;
    w.on_a11y_detach = uia_detach;
    return true;
}
} // namespace pui
