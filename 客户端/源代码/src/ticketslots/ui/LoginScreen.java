package ticketslots.ui;

import java.awt.BorderLayout;
import java.awt.FlowLayout;
import java.awt.GridBagLayout;
import javax.swing.JButton;
import javax.swing.JLabel;
import javax.swing.JPanel;
import javax.swing.JPasswordField;
import javax.swing.JTextField;
import ticketslots.AppEvent;
import ticketslots.FrontendSlot;

final class LoginScreen implements ScreenView {
    private final FrontendSlot slot;
    private final JPanel root;
    private final JTextField username = new JTextField("java_user", 18);
    private final JPasswordField password = new JPasswordField("java123", 18);
    private final JButton register = TemplateLibrary.secondaryButton("注册账号");
    private final JButton login = TemplateLibrary.primaryButton("登录");
    private final JButton logout = TemplateLibrary.secondaryButton("退出登录");
    private final JButton whoami = TemplateLibrary.secondaryButton("查看身份");
    private final JLabel session = new JLabel("未登录");

    LoginScreen(FrontendSlot slot) {
        this.slot = slot;
        root = TemplateLibrary.page("账户登录",
                "采用挑战应答认证，密码不会以明文发送。");

        JPanel form = TemplateLibrary.surface();
        TemplateLibrary.addFormRow(form, 0, "用户名", username);
        TemplateLibrary.addFormRow(form, 1, "密码", password);

        JPanel actions = new JPanel(new FlowLayout(FlowLayout.LEFT, 8, 0));
        actions.setOpaque(false);
        actions.add(login);
        actions.add(register);
        actions.add(logout);
        actions.add(whoami);
        TemplateLibrary.addFormRow(form, 2, "操作", actions);
        TemplateLibrary.addFormRow(form, 3, "会话", session);

        login.addActionListener(event -> submitCredentials(AppEvent.LOGIN_REQUEST));
        register.addActionListener(event -> submitCredentials(AppEvent.REGISTER_REQUEST));
        logout.addActionListener(event ->
                slot.submit(AppEvent.of(AppEvent.LOGOUT_REQUEST)));
        whoami.addActionListener(event ->
                slot.submit(AppEvent.of(AppEvent.WHOAMI_REQUEST)));

        JPanel holder = new JPanel(new BorderLayout());
        holder.setOpaque(false);
        holder.add(form, BorderLayout.NORTH);
        root.add(holder, BorderLayout.CENTER);
        updateSession(false, "", "", "未登录");
    }

    private void submitCredentials(String type) {
        char[] secret = password.getPassword();
        slot.submit(AppEvent.of(type, "username", username.getText().trim())
                .with("password", secret));
    }

    void updateSession(boolean authenticated, String user, String userId,
                       String detail) {
        session.setText(authenticated
                ? "已登录：" + user + "（用户编号 " + userId + "）"
                : (detail == null || detail.isBlank()
                        ? "未登录" : UiText.detail(detail)));
        session.setForeground(authenticated
                ? TemplateLibrary.ACCENT : TemplateLibrary.WARNING);
        logout.setEnabled(authenticated);
        whoami.setEnabled(authenticated);
    }

    @Override
    public String id() {
        return "login";
    }

    @Override
    public JPanel component() {
        return root;
    }
}
