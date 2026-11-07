package ticketslots.ui;

import java.awt.BorderLayout;
import java.awt.FlowLayout;
import java.awt.GridBagLayout;
import javax.swing.JButton;
import javax.swing.JLabel;
import javax.swing.JPanel;
import javax.swing.JTextField;
import ticketslots.AppEvent;
import ticketslots.FrontendSlot;

final class ConnectionScreen implements ScreenView {
    private final FrontendSlot slot;
    private final JPanel root;
    private final JTextField host = new JTextField("127.0.0.1", 18);
    private final JTextField port = new JTextField("9101", 8);
    private final JButton connect = TemplateLibrary.primaryButton("连接");
    private final JButton disconnect = TemplateLibrary.secondaryButton("断开连接");
    private final JLabel status = new JLabel("未连接");

    ConnectionScreen(FrontendSlot slot) {
        this.slot = slot;
        root = TemplateLibrary.page("服务器连接",
                "建立并保持所有业务任务共用的网络连接。");

        JPanel form = TemplateLibrary.surface();
        form.setLayout(new GridBagLayout());
        TemplateLibrary.addFormRow(form, 0, "主机地址", host);
        TemplateLibrary.addFormRow(form, 1, "端口", port);

        JPanel actions = new JPanel(new FlowLayout(FlowLayout.LEFT, 8, 0));
        actions.setOpaque(false);
        actions.add(connect);
        actions.add(disconnect);
        TemplateLibrary.addFormRow(form, 2, "操作", actions);
        TemplateLibrary.addFormRow(form, 3, "状态", status);

        connect.addActionListener(event -> {
            int value;
            try {
                value = Integer.parseInt(port.getText().trim());
            } catch (NumberFormatException error) {
                status.setText("端口必须是数字");
                return;
            }
            slot.submit(AppEvent.of(AppEvent.CONNECT_REQUEST, "host",
                    host.getText().trim())
                    .with("port", value));
        });
        disconnect.addActionListener(event ->
                slot.submit(AppEvent.of(AppEvent.DISCONNECT_REQUEST)));

        JPanel holder = new JPanel(new BorderLayout());
        holder.setOpaque(false);
        holder.add(form, BorderLayout.NORTH);
        root.add(holder, BorderLayout.CENTER);
        updateConnection(false, "未连接");
    }

    void updateConnection(boolean connected, String detail) {
        String translated = UiText.reason(detail);
        status.setText(connected ? "已连接"
                : (translated.isBlank() ? "未连接" : translated));
        status.setForeground(connected ? TemplateLibrary.ACCENT : TemplateLibrary.WARNING);
        connect.setEnabled(!connected);
        disconnect.setEnabled(connected);
        host.setEnabled(!connected);
        port.setEnabled(!connected);
        if (!translated.isBlank()) {
            status.setToolTipText(translated);
        }
    }

    @Override
    public String id() {
        return "connection";
    }

    @Override
    public JPanel component() {
        return root;
    }
}
