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
import ticketslots.model.TicketType;

final class BuyScreen implements ScreenView {
    private final FrontendSlot slot;
    private final JPanel root;
    private final JLabel event = new JLabel("-");
    private final JLabel ticket = new JLabel("-");
    private final JLabel price = new JLabel("-");
    private final JLabel stock = new JLabel("-");
    private final JSpinner quantity = new JSpinner(new SpinnerNumberModel(1, 1, 10, 1));
    private final JButton submit = TemplateLibrary.primaryButton("提交购票");
    private final JButton refresh = TemplateLibrary.secondaryButton("刷新目录");
    private final JLabel status = new JLabel("未选择票种");
    private final JLabel orderNo = new JLabel("-");

    BuyScreen(FrontendSlot slot) {
        this.slot = slot;
        root = TemplateLibrary.page("确认购票",
                "核对所选票种和购票数量后提交订单。");

        JPanel summary = TemplateLibrary.summaryStrip(
                TemplateLibrary.summary("活动", event),
                TemplateLibrary.summary("票种", ticket),
                TemplateLibrary.summary("票价", price),
                TemplateLibrary.summary("余票", stock));

        JPanel form = TemplateLibrary.surface();
        TemplateLibrary.addFormRow(form, 0, "数量", quantity);

        JPanel actions = new JPanel(new FlowLayout(FlowLayout.LEFT, 8, 0));
        actions.setOpaque(false);
        actions.add(submit);
        actions.add(refresh);
        TemplateLibrary.addFormRow(form, 1, "操作", actions);
        TemplateLibrary.addFormRow(form, 2, "状态", status);
        TemplateLibrary.addFormRow(form, 3, "订单号", orderNo);

        JPanel content = new JPanel(new BorderLayout(0, 18));
        content.setOpaque(false);
        content.add(summary, BorderLayout.NORTH);
        content.add(form, BorderLayout.CENTER);
        root.add(content, BorderLayout.CENTER);

        submit.addActionListener(event -> slot.submit(
                AppEvent.of(AppEvent.RESERVE_REQUEST, "quantity",
                        ((Number) quantity.getValue()).intValue())));
        refresh.addActionListener(action ->
                slot.submit(AppEvent.of(AppEvent.CATALOG_REQUEST)));
    }

    void setSelectedTicket(TicketType selected) {
        if (selected == null) {
            event.setText("-");
            ticket.setText("-");
            price.setText("-");
            stock.setText("-");
            quantity.setModel(new SpinnerNumberModel(1, 1, 1, 1));
            submit.setEnabled(false);
            return;
        }
        event.setText(selected.eventName());
        ticket.setText(selected.ticketName());
        price.setText(money(selected.priceCents()));
        stock.setText(String.valueOf(selected.remaining()));
        int max = Math.max(1, Math.min(10, selected.remaining()));
        quantity.setModel(new SpinnerNumberModel(1, 1, max, 1));
        submit.setEnabled(selected.remaining() > 0);
        status.setText("可以提交购票");
    }

    void setStatus(String value, String order) {
        status.setText(value == null || value.isBlank() ? "未选择票种" : value);
        orderNo.setText(order == null || order.isBlank() ? "-" : order);
    }

    private static String money(long cents) {
        return String.format("%d.%02d", cents / 100, Math.abs(cents % 100));
    }

    @Override
    public String id() {
        return "buy";
    }

    @Override
    public JPanel component() {
        return root;
    }
}
