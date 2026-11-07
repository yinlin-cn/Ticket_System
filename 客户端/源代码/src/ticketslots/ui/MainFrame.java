package ticketslots.ui;

import java.awt.BorderLayout;
import java.awt.CardLayout;
import java.awt.Color;
import java.awt.Dimension;
import java.awt.FlowLayout;
import java.awt.Font;
import java.awt.GridLayout;
import java.awt.event.WindowAdapter;
import java.awt.event.WindowEvent;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import javax.swing.BorderFactory;
import javax.swing.Box;
import javax.swing.BoxLayout;
import javax.swing.JButton;
import javax.swing.JFrame;
import javax.swing.JLabel;
import javax.swing.JPanel;
import javax.swing.WindowConstants;
import ticketslots.AppEvent;
import ticketslots.AppState;
import ticketslots.FrontendSlot;
import ticketslots.model.OrderRecord;
import ticketslots.model.TicketType;

public final class MainFrame extends JFrame implements FrontendSlot {
    private static final Color NAV = new Color(29, 43, 50);
    private static final Color NAV_ACTIVE = new Color(43, 65, 72);

    private final CardLayout cards = new CardLayout();
    private final JPanel content = new JPanel(cards);
    private final Map<String, ScreenView> screens = new LinkedHashMap<>();
    private final Map<String, JButton> navigation = new LinkedHashMap<>();
    private final JLabel connection = new JLabel("未连接");
    private final JLabel session = new JLabel("未登录");
    private final JLabel busy = new JLabel("空闲");
    private FrontendSlot runtimeBus;
    private Runnable closeAction = () -> {
    };
    private String currentScreen = "connection";

    public MainFrame() {
        super("票务服务台");
        setDefaultCloseOperation(WindowConstants.DO_NOTHING_ON_CLOSE);
        setMinimumSize(new Dimension(1000, 680));
        setSize(1200, 780);
        setLocationRelativeTo(null);
        build();
        addWindowListener(new WindowAdapter() {
            @Override
            public void windowClosing(WindowEvent event) {
                closeAction.run();
            }
        });
    }

    private void build() {
        JPanel root = new JPanel(new BorderLayout());
        root.setBackground(TemplateLibrary.CANVAS);
        root.add(buildNavigation(), BorderLayout.WEST);
        root.add(buildMainArea(), BorderLayout.CENTER);
        setContentPane(root);
    }

    private JPanel buildNavigation() {
        JPanel nav = new JPanel(new BorderLayout());
        nav.setBackground(NAV);
        nav.setPreferredSize(new Dimension(190, 0));
        nav.setBorder(BorderFactory.createEmptyBorder(24, 16, 20, 16));

        JLabel brand = new JLabel("票务服务台");
        brand.setForeground(Color.WHITE);
        brand.setFont(brand.getFont().deriveFont(Font.BOLD, 17f));
        brand.setBorder(BorderFactory.createEmptyBorder(0, 10, 24, 0));

        JPanel items = new JPanel();
        items.setLayout(new BoxLayout(items, BoxLayout.Y_AXIS));
        items.setOpaque(false);
        addScreen(items, new ConnectionScreen(this), UiText.screenName("connection"));
        addScreen(items, new LoginScreen(this), UiText.screenName("login"));
        addScreen(items, new CatalogScreen(this), UiText.screenName("catalog"));
        addScreen(items, new BuyScreen(this), UiText.screenName("buy"));
        addScreen(items, new OrdersScreen(this), UiText.screenName("orders"));
        addScreen(items, new RefundScreen(this), UiText.screenName("refund"));
        addScreen(items, new LogScreen(), UiText.screenName("log"));

        JLabel hint = new JLabel("持久化分帧会话");
        hint.setForeground(new Color(154, 169, 173));
        hint.setBorder(BorderFactory.createEmptyBorder(20, 10, 0, 0));

        nav.add(brand, BorderLayout.NORTH);
        nav.add(items, BorderLayout.CENTER);
        nav.add(hint, BorderLayout.SOUTH);
        return nav;
    }

    private void addScreen(JPanel navItems, ScreenView screen, String label) {
        screens.put(screen.id(), screen);
        content.add(screen.component(), screen.id());

        JButton button = new JButton(label);
        button.setHorizontalAlignment(JButton.LEFT);
        button.setForeground(new Color(218, 226, 228));
        button.setBackground(NAV);
        button.setBorder(BorderFactory.createEmptyBorder(10, 12, 10, 12));
        button.setAlignmentX(JButton.LEFT_ALIGNMENT);
        button.setMaximumSize(new Dimension(Integer.MAX_VALUE, 44));
        TemplateLibrary.flatButton(button);
        button.addActionListener(event -> submit(
                AppEvent.of(AppEvent.NAVIGATE_REQUEST, "screen", screen.id())
                        .with("source", "sidebar")));
        navigation.put(screen.id(), button);
        navItems.add(button);
        navItems.add(Box.createVerticalStrut(6));
    }

    private JPanel buildMainArea() {
        JPanel main = new JPanel(new BorderLayout());
        main.setBackground(TemplateLibrary.CANVAS);
        main.add(buildHeader(), BorderLayout.NORTH);
        main.add(content, BorderLayout.CENTER);
        return main;
    }

    private JPanel buildHeader() {
        JPanel header = new JPanel(new BorderLayout());
        header.setBackground(Color.WHITE);
        header.setBorder(BorderFactory.createCompoundBorder(
                BorderFactory.createMatteBorder(0, 0, 1, 0, TemplateLibrary.BORDER),
                BorderFactory.createEmptyBorder(12, 26, 12, 26)));

        busy.setForeground(TemplateLibrary.MUTED);
        busy.setFont(busy.getFont().deriveFont(12f));
        header.add(busy, BorderLayout.WEST);

        JPanel state = new JPanel(new FlowLayout(FlowLayout.RIGHT, 18, 0));
        state.setOpaque(false);
        connection.setForeground(TemplateLibrary.WARNING);
        session.setForeground(TemplateLibrary.MUTED);
        state.add(connection);
        state.add(session);
        header.add(state, BorderLayout.EAST);
        return header;
    }

    public void attach(FrontendSlot runtimeBus) {
        this.runtimeBus = runtimeBus;
    }

    public void setCloseAction(Runnable action) {
        closeAction = action == null ? () -> {
        } : action;
    }

    @Override
    public void submit(AppEvent event) {
        FrontendSlot target = runtimeBus;
        if (target != null) {
            target.submit(event);
        }
    }

    private void showScreen(String screenId) {
        ScreenView screen = screens.get(screenId);
        if (screen == null) {
            return;
        }
        currentScreen = screenId;
        cards.show(content, screenId);
        navigation.forEach((id, button) -> {
            boolean active = id.equals(screenId);
            button.setBackground(active ? NAV_ACTIVE : NAV);
            button.setForeground(active ? Color.WHITE : new Color(218, 226, 228));
        });
        screen.activate();
    }

    public void applyState(AppState.Snapshot snapshot) {
        ConnectionState connectionState = new ConnectionState(snapshot);
        updateConnection(connectionState.connected, connectionState.detail);
        AuthState authState = new AuthState(snapshot);
        updateSession(authState.authenticated, authState.username,
                authState.userId, authState.detail);

        updateCatalog(snapshot.catalog());
        updateSelectedTicket(snapshot.selectedTicket());
        updatePurchaseStatus(status(snapshot, "purchase",
                snapshot.selectedTicket() == null
                        ? "未选择票种" : "可以提交购票"),
                snapshot.lastOrderNo());

        updateOrders(snapshot.orders());
        updateOrderStatus(status(snapshot, "orders",
                snapshot.orders().isEmpty()
                        ? "尚未加载订单"
                        : UiText.orders(snapshot.orders().size())));
        updateRefundOrder(snapshot.selectedOrder());
        updateRefundStatus(status(snapshot, "refund",
                snapshot.selectedOrder() == null
                        ? "未选择订单"
                        : UiText.orderStatus(snapshot.selectedOrder().status())));

        if (!currentScreen.equals(snapshot.currentScreen())) {
            showScreen(snapshot.currentScreen());
        }
    }

    private static String status(AppState.Snapshot snapshot, String taskId,
                                 String fallback) {
        AppState.TaskStatus task = snapshot.task(taskId);
        if (task == null || task.status() == null || task.status().isBlank()
                || "IDLE".equals(task.status())) {
            return fallback;
        }
        String label = UiText.taskStatus(task.status());
        String detail = UiText.detail(task.detail());
        return detail.isBlank() ? label : label + " / " + detail;
    }

    private record ConnectionState(boolean connected, String detail) {
        private ConnectionState(AppState.Snapshot snapshot) {
            this(snapshot.connection().connected(), snapshot.connection().detail());
        }
    }

    private record AuthState(boolean authenticated, String username,
                             String userId, String detail) {
        private AuthState(AppState.Snapshot snapshot) {
            this(snapshot.session().authenticated(),
                    snapshot.session().username(),
                    snapshot.session().userId(),
                    snapshot.session().detail());
        }
    }

    public void updateConnection(boolean connected, String detail) {
        connection.setText(connected ? "在线" : "未连接");
        connection.setForeground(connected
                ? TemplateLibrary.ACCENT : TemplateLibrary.WARNING);
        connection.setToolTipText(UiText.reason(detail));
        ((ConnectionScreen) screens.get("connection"))
                .updateConnection(connected, detail);
    }

    public void updateSession(boolean authenticated, String username,
                              String userId, String detail) {
        session.setText(authenticated
                ? "已登录：" + username + " #" + userId : "未登录");
        session.setToolTipText(UiText.reason(detail));
        ((LoginScreen) screens.get("login"))
                .updateSession(authenticated, username, userId, detail);
    }

    public void updateBusy(String detail) {
        busy.setText(detail == null || detail.isBlank() ? "空闲" : detail);
        busy.setForeground(detail == null || detail.isBlank()
                ? TemplateLibrary.MUTED : TemplateLibrary.ACCENT);
    }

    public void updateCatalog(List<TicketType> tickets) {
        ((CatalogScreen) screens.get("catalog")).setTickets(tickets);
    }

    public void updateSelectedTicket(TicketType ticket) {
        ((BuyScreen) screens.get("buy")).setSelectedTicket(ticket);
    }

    public void updatePurchaseStatus(String status, String orderNo) {
        ((BuyScreen) screens.get("buy")).setStatus(status, orderNo);
    }

    public void updateOrders(List<OrderRecord> orders) {
        ((OrdersScreen) screens.get("orders")).setOrders(orders);
    }

    public void updateOrderStatus(String status) {
        ((OrdersScreen) screens.get("orders")).setStatus(status);
    }

    public void updateRefundOrder(OrderRecord order) {
        ((RefundScreen) screens.get("refund")).setOrder(order);
    }

    public void updateRefundStatus(String status) {
        ((RefundScreen) screens.get("refund")).setStatus(status);
    }

    public void appendLog(String line) {
        ((LogScreen) screens.get("log")).append(line);
    }

    public String currentScreen() {
        return currentScreen;
    }
}
