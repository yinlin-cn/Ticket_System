package ticketslots.ui;

import java.awt.BorderLayout;
import java.awt.event.MouseAdapter;
import java.awt.event.MouseEvent;
import java.util.ArrayList;
import java.util.List;
import javax.swing.JButton;
import javax.swing.JLabel;
import javax.swing.JPanel;
import javax.swing.JScrollPane;
import javax.swing.JTable;
import javax.swing.ListSelectionModel;
import javax.swing.table.DefaultTableModel;
import ticketslots.AppEvent;
import ticketslots.FrontendSlot;
import ticketslots.model.OrderRecord;

final class OrdersScreen implements ScreenView {
    private final FrontendSlot slot;
    private final JPanel root;
    private final DefaultTableModel model = new DefaultTableModel(
            new Object[] {"订单号", "票种编号", "数量", "金额", "状态",
                    "已退数量", "创建时间"}, 0) {
        @Override
        public boolean isCellEditable(int row, int column) {
            return false;
        }
    };
    private final JTable table = new JTable(model);
    private final JButton refresh = TemplateLibrary.secondaryButton("刷新");
    private final JButton open = TemplateLibrary.primaryButton("查看所选订单");
    private final JLabel status = new JLabel("尚未加载订单");
    private final List<OrderRecord> orders = new ArrayList<>();

    OrdersScreen(FrontendSlot slot) {
        this.slot = slot;
        root = TemplateLibrary.page("我的订单",
                "查看订单状态，并将选中的订单带入退票流程。");

        JPanel toolbar = TemplateLibrary.toolbar();
        toolbar.add(refresh);
        toolbar.add(open);
        toolbar.add(status);

        table.setFillsViewportHeight(true);
        table.setRowHeight(26);
        table.setAutoResizeMode(JTable.AUTO_RESIZE_OFF);
        table.setSelectionMode(ListSelectionModel.SINGLE_SELECTION);
        table.getTableHeader().setReorderingAllowed(false);
        sizeColumns(180, 100, 55, 90, 100, 80, 160);
        JScrollPane scroll = new JScrollPane(table);
        scroll.setBorder(TemplateLibrary.sectionBorder());

        JPanel content = new JPanel(new BorderLayout(0, 12));
        content.setOpaque(false);
        content.add(toolbar, BorderLayout.NORTH);
        content.add(scroll, BorderLayout.CENTER);
        root.add(content, BorderLayout.CENTER);

        refresh.addActionListener(event ->
                slot.submit(AppEvent.of(AppEvent.ORDERS_REQUEST)));
        open.addActionListener(event -> openSelected());
        table.addMouseListener(new MouseAdapter() {
            @Override
            public void mouseClicked(MouseEvent event) {
                if (event.getClickCount() == 2) {
                    openSelected();
                }
            }
        });
    }

    void setOrders(List<OrderRecord> values) {
        orders.clear();
        orders.addAll(values);
        model.setRowCount(0);
        for (OrderRecord order : values) {
            model.addRow(new Object[] {
                    order.orderNo(), order.ticketType(), order.quantity(),
                    money(order.amountCents()), UiText.orderStatus(order.status()),
                    order.refundedQuantity(), order.createdAt()
            });
        }
        status.setText(UiText.orders(values.size()));
    }

    void setStatus(String value) {
        status.setText(value == null ? "" : value);
    }

    private void openSelected() {
        int row = table.getSelectedRow();
        if (row < 0 || row >= orders.size()) {
            status.setText("请先选择订单");
            return;
        }
        slot.submit(AppEvent.of(AppEvent.ORDER_SELECTED, "order", orders.get(row)));
    }

    private static String money(long cents) {
        return String.format("%d.%02d", cents / 100, Math.abs(cents % 100));
    }

    private void sizeColumns(int... widths) {
        for (int column = 0; column < widths.length; column++) {
            table.getColumnModel().getColumn(column).setPreferredWidth(widths[column]);
        }
    }

    @Override
    public String id() {
        return "orders";
    }

    @Override
    public JPanel component() {
        return root;
    }
}
