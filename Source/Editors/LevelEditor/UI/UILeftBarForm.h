#pragma once
class UILeftBarForm: public xrUI
{
public:
    UILeftBarForm();
    virtual ~UILeftBarForm();
    virtual void Draw();
    IC bool      IsUseSnapList() const
    {
        return m_UseSnapList;
    }
    // Setter used by the wallmark Move control so it can transparently auto-
    // enable the snap-list gate during a drag and restore on release. Users
    // shouldn't have to flip the LeftBar checkbox themselves to translate a
    // wallmark — the move path already knows which surfaces are valid.
    IC void SetUseSnapList(bool b)
    {
        m_UseSnapList = b;
    }
    IC bool IsSnapListMode() const
    {
        return m_SnapListMode;
    }

private:
    bool m_UseSnapList;
    bool m_SnapListMode;
    int  m_SnapItem_Current;
};
