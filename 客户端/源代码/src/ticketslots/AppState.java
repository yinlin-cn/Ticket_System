package ticketslots;

import java.util.ArrayList;
import java.util.Collections;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import ticketslots.model.OrderRecord;
import ticketslots.model.TicketType;

/**
 * Shared application context owned by the long-task runtime.
 *
 * <p>Tasks read and update their own business partition here. Screens never
 * mutate this object directly; they receive immutable snapshots through the
 * UI port after the runtime finishes an event.</p>
 */
public final class AppState {
    public record ConnectionState(
            boolean connected,
            String host,
            int port,
            String detail) {
    }

    public record SessionState(
            boolean authenticated,
            String username,
            String userId,
            String token,
            String detail) {
    }

    public record TaskStatus(
            String task,
            String status,
            String reference,
            String detail) {
        public static TaskStatus idle(String task) {
            return new TaskStatus(task, "IDLE", "", "");
        }

        public String summary() {
            if (detail == null || detail.isBlank()) {
                return status == null ? "" : status;
            }
            if (status == null || status.isBlank()) {
                return detail;
            }
            return status + " / " + detail;
        }
    }

    public record Snapshot(
            ConnectionState connection,
            SessionState session,
            List<TicketType> catalog,
            TicketType selectedTicket,
            List<OrderRecord> orders,
            OrderRecord selectedOrder,
            String lastOrderNo,
            String currentScreen,
            Map<String, TaskStatus> taskStatuses) {
        public TaskStatus task(String taskId) {
            return taskStatuses.getOrDefault(taskId, TaskStatus.idle(taskId));
        }
    }

    private ConnectionState connection =
            new ConnectionState(false, "127.0.0.1", 9101, "Offline");
    private SessionState session =
            new SessionState(false, "", "", "", "Not signed in");
    private List<TicketType> catalog = List.of();
    private TicketType selectedTicket;
    private List<OrderRecord> orders = List.of();
    private OrderRecord selectedOrder;
    private String lastOrderNo = "";
    private String currentScreen = "connection";
    private final Map<String, TaskStatus> taskStatuses = new HashMap<>();

    public synchronized boolean connected() {
        return connection.connected();
    }

    public synchronized ConnectionState connection() {
        return connection;
    }

    public synchronized void connectionOpen(String host, int port) {
        connection = new ConnectionState(true, safe(host), port, "connected");
    }

    public synchronized void connectionClosed(String detail) {
        connection = new ConnectionState(false, connection.host(), connection.port(),
                safe(detail, "Offline"));
        clearSession("offline");
        clearBusinessData();
    }

    public synchronized void connectionFailed(String detail) {
        connection = new ConnectionState(false, connection.host(), connection.port(),
                safe(detail, "connection failed"));
    }

    public synchronized boolean authenticated() {
        return session.authenticated();
    }

    public synchronized String username() {
        return session.username();
    }

    public synchronized String userId() {
        return session.userId();
    }

    public synchronized String token() {
        return session.token();
    }

    public synchronized SessionState sessionState() {
        return session;
    }

    public synchronized void startSession(String newUsername, String newUserId,
                                          String newToken) {
        session = new SessionState(true, safe(newUsername), safe(newUserId),
                safe(newToken), "authenticated");
        clearOrders();
        lastOrderNo = "";
    }

    public synchronized void clearSession(String detail) {
        session = new SessionState(false, "", "", "",
                safe(detail, "Not signed in"));
        clearOrders();
        lastOrderNo = "";
    }

    public synchronized void sessionFailed(String detail) {
        clearSession(safe(detail, "authentication failed"));
    }

    public synchronized List<TicketType> catalog() {
        return catalog;
    }

    public synchronized void catalog(List<TicketType> values) {
        catalog = immutableList(values);
        if (selectedTicket == null) {
            return;
        }
        TicketType refreshed = null;
        for (TicketType ticket : catalog) {
            if (ticket.id().equals(selectedTicket.id())) {
                refreshed = ticket;
                break;
            }
        }
        selectedTicket = refreshed;
    }

    public synchronized TicketType selectedTicket() {
        return selectedTicket;
    }

    public synchronized void selectedTicket(TicketType ticket) {
        if (ticket == null) {
            selectedTicket = null;
            return;
        }
        TicketType latest = ticket;
        for (TicketType candidate : catalog) {
            if (candidate.id().equals(ticket.id())) {
                latest = candidate;
                break;
            }
        }
        selectedTicket = latest;
    }

    public synchronized List<OrderRecord> orders() {
        return orders;
    }

    public synchronized void orders(List<OrderRecord> values) {
        orders = immutableList(values);
        if (selectedOrder == null) {
            return;
        }
        OrderRecord refreshed = findOrder(selectedOrder.orderNo());
        selectedOrder = refreshed;
        if (refreshed != null) {
            lastOrderNo = refreshed.orderNo();
        }
    }

    public synchronized OrderRecord selectedOrder() {
        return selectedOrder;
    }

    public synchronized void selectedOrder(OrderRecord order) {
        selectedOrder = order;
        if (order != null) {
            lastOrderNo = order.orderNo();
        }
    }

    public synchronized void orderStatus(OrderRecord order) {
        if (order == null) {
            return;
        }
        List<OrderRecord> next = new ArrayList<>(orders);
        boolean replaced = false;
        for (int i = 0; i < next.size(); i++) {
            if (next.get(i).orderNo().equals(order.orderNo())) {
                next.set(i, order);
                replaced = true;
                break;
            }
        }
        if (!replaced) {
            next.add(order);
        }
        orders = List.copyOf(next);
        selectedOrder = order;
        lastOrderNo = order.orderNo();
    }

    public synchronized String lastOrderNo() {
        return lastOrderNo;
    }

    public synchronized void lastOrderNo(String orderNo) {
        lastOrderNo = safe(orderNo);
    }

    public synchronized String currentScreen() {
        return currentScreen;
    }

    synchronized void showScreen(String screenId) {
        if (screenId != null && !screenId.isBlank()) {
            currentScreen = screenId;
        }
    }

    public synchronized void updateTask(String taskId, String status,
                                        String reference, String detail) {
        taskStatuses.put(safe(taskId),
                new TaskStatus(safe(taskId), safe(status),
                        safe(reference), safe(detail)));
    }

    public synchronized TaskStatus taskStatus(String taskId) {
        return taskStatuses.getOrDefault(taskId, TaskStatus.idle(taskId));
    }

    public synchronized Snapshot snapshot() {
        return new Snapshot(
                connection,
                session,
                catalog,
                selectedTicket,
                orders,
                selectedOrder,
                lastOrderNo,
                currentScreen,
                Collections.unmodifiableMap(new HashMap<>(taskStatuses)));
    }

    private OrderRecord findOrder(String orderNo) {
        if (orderNo == null || orderNo.isBlank()) {
            return null;
        }
        for (OrderRecord order : orders) {
            if (orderNo.equals(order.orderNo())) {
                return order;
            }
        }
        return null;
    }

    private void clearOrders() {
        orders = List.of();
        selectedOrder = null;
    }

    private void clearBusinessData() {
        catalog = List.of();
        selectedTicket = null;
        clearOrders();
        lastOrderNo = "";
        taskStatuses.clear();
    }

    private static <T> List<T> immutableList(List<T> values) {
        return values == null
                ? List.of()
                : Collections.unmodifiableList(new ArrayList<>(values));
    }

    private static String safe(String value) {
        return value == null ? "" : value;
    }

    private static String safe(String value, String fallback) {
        return value == null || value.isBlank() ? fallback : value;
    }
}
