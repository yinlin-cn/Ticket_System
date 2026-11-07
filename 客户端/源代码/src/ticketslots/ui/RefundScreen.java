package ticketslots.ui;

import java.awt.BorderLayout;
import java.awt.FlowLayout;
import java.awt.GridBagLayout;
import javax.swing.JButton;
import javax.swing.JLabel;
import javax.swing.JPanel;
import javax.swing.JSpinner;
import javax.swing.SpinnerNumberModel;
import ticketslots.AppEvent;
import ticketslots.FrontendSlot;
import ticketslots.model.OrderRecord;

final class RefundScreen implements ScreenView {
    private final FrontendSlot slot;
    private final JPanel root;
    private final JLabel orderNo = new JLabel("-");
    private final JLabel ticketType = new JLabel("-");
    private final JLabel purchased = new JLabel("-");
    private final JLabel refunded = new JLabel("-");
    private final JSpinner quantity = new JSpinner(new SpinnerNumberModel(1, 1, 1, 1));
    private final JButton submit = TemplateLibrary.primaryButton("提交退票");
    private final JButton orders = TemplateLibrary.secondaryButton("返回订单");
    private final JLabel status = new JLabel("未选择订单");
    private OrderRecord selected;

    RefundScreen(FrontendSlot slot) {
        this.slot = slot;
        root = TemplateLibrary.page("申请退票",
                "服务器会根据原始订单校验退票资格和可退数量。");

        JPanel summary = TemplateLibrary.summaryStrip(
                TemplateLibrary.summary("订单号", orderNo),
                TemplateLibrary.summary("票种编号", ticketType),
                TemplateLibrary.summary("购买数量", purchased),
                TemplateLibrary.summary("已退数量", refunded));

        JPanel form = TemplateLibrary.surface();
        TemplateLibrary.addFormRow(form, 0, "退票数量", quantity);

        JPanel actions = new JPanel(new FlowLayout(FlowLayout.LEFT, 8, 0));
        actions.setOpaque(false);
        actions.add(submit);
        actions.add(orders);
        TemplateLibrary.addFormRow(form, 1, "操作", actions);
        TemplateLibrary.addFormRow(form, 2, "状态", status);

        JPanel content = new JPanel(new BorderLayout(0, 18));
        content.setOpaque(false);
        content.add(summary, BorderLayout.NORTH);
        content.add(form, BorderLayout.CENTER);
        root.add(content, BorderLayout.CENTER);

        submit.addActionListener(event -> slot.submit(
                AppEvent.of(AppEvent.REFUND_REQUEST, "quantity",
                        ((Number) quantity.getValue()).intValue())));
        orders.addActionListener(event -> {
            slot.submit(AppEvent.of(AppEvent.ORDERS_REQUEST));
            slot.submit(AppEvent.of(AppEvent.NAVIGATE_REQUEST, "screen", "orders"));
        });
    }

    void setOrder(OrderRecord order) {
        selected = order;
        if (order == null) {
            orderNo.setText("-");
            ticketType.setText("-");
            purchased.setText("-");
            refunded.setText("-");
            quantity.setModel(new SpinnerNumberModel(1, 1, 1, 1));
            submit.setEnabled(false);
            status.setText("未选择订单");
            return;
        }
        orderNo.setText(order.orderNo());
        ticketType.setText(order.ticketType());
        purchased.setText(String.valueOf(order.quantity()));
        refunded.setText(String.valueOf(order.refundedQuantity()));
        int available = Math.max(1, Math.min(10,
                order.quantity() - order.refundedQuantity()));
        quantity.setModel(new SpinnerNumberModel(1, 1, available, 1));
        submit.setEnabled(order.quantity() > order.refundedQuantity());
        status.setText(UiText.orderStatus(order.status()));
    }

    void setStatus(String value) {
        status.setText(value == null || value.isBlank() ? "未选择订单" : value);
    }

    @Override
    public String id() {
        return "refund";
    }

    @Override
    public JPanel component() {
        return root;
    }
}
