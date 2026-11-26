// Stand-ins for declarations owned by SDK headers.
struct MSG {
    int message_id;
};
struct XEvent {
    int send_event;
};
struct wl_registry_listener {
    void (*global_remove)();
};
struct AtspiAccessible {
    void* object_ref;
};
struct IRawElementProvider {
    virtual int get_BoundingRectangle() = 0;
};
