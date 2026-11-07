package ticketslots;

import java.util.Collections;
import java.util.LinkedHashMap;
import java.util.Map;

/**
 * Typed message passed between screens, the long-task runtime and protocol
 * events. Data stays string-friendly so new slots can be added without
 * changing the transport.
 */
public final class AppEvent {
    public static final String CONNECT_REQUEST = "ui.connect";
    public static final String DISCONNECT_REQUEST = "ui.disconnect";
    public static final String REGISTER_REQUEST = "ui.register";
    public static final String LOGIN_REQUEST = "ui.login";
    public static final String LOGOUT_REQUEST = "ui.logout";
    public static final String WHOAMI_REQUEST = "ui.whoami";
    public static final String PING_REQUEST = "ui.ping";
    public static final String NAVIGATE_REQUEST = "ui.navigate";

    public static final String CATALOG_REQUEST = "ui.catalog";
    public static final String TICKET_SELECTED = "ui.ticket.selected";
    public static final String RESERVE_REQUEST = "ui.reserve";
    public static final String ORDERS_REQUEST = "ui.orders";
    public static final String ORDER_STATUS_REQUEST = "ui.order.status";
    public static final String ORDER_SELECTED = "ui.order.selected";
    public static final String REFUND_REQUEST = "ui.refund";

    public static final String NETWORK_CONNECTED = "network.connected";
    public static final String NETWORK_DISCONNECTED = "network.disconnected";
    public static final String NETWORK_ERROR = "network.error";
    public static final String WIRE = "wire";
    public static final String SERVER_RESPONSE = "protocol.response";
    public static final String REQUEST_TIMEOUT = "request.timeout";

    private final String type;
    private final Map<String, Object> data;

    private AppEvent(String type, Map<String, Object> data) {
        this.type = type;
        this.data = Collections.unmodifiableMap(new LinkedHashMap<>(data));
    }

    public static AppEvent of(String type) {
        return new AppEvent(type, Map.of());
    }

    public static AppEvent of(String type, String key, Object value) {
        Map<String, Object> values = new LinkedHashMap<>();
        values.put(key, value);
        return new AppEvent(type, values);
    }

    public AppEvent with(String key, Object value) {
        Map<String, Object> values = new LinkedHashMap<>(data);
        values.put(key, value);
        return new AppEvent(type, values);
    }

    public String type() {
        return type;
    }

    public Map<String, Object> data() {
        return data;
    }

    public String get(String key) {
        Object value = data.get(key);
        return value == null ? "" : String.valueOf(value);
    }

    public String get(String key, String fallback) {
        String value = get(key);
        return value.isEmpty() ? fallback : value;
    }

    public Object value(String key) {
        return data.get(key);
    }

    public boolean getBoolean(String key, boolean fallback) {
        Object value = data.get(key);
        if (value instanceof Boolean booleanValue) {
            return booleanValue;
        }
        if (value != null) {
            return Boolean.parseBoolean(String.valueOf(value));
        }
        return fallback;
    }

    public int getInt(String key, int fallback) {
        try {
            return Integer.parseInt(get(key));
        } catch (NumberFormatException ignored) {
            return fallback;
        }
    }

    public long getLong(String key, long fallback) {
        try {
            return Long.parseLong(get(key));
        } catch (NumberFormatException ignored) {
            return fallback;
        }
    }

    @Override
    public String toString() {
        return type + " " + data;
    }
}
