package ticketslots.tasks;

import java.security.GeneralSecurityException;
import java.util.Arrays;
import ticketslots.AppEvent;
import ticketslots.AuthCrypto;
import ticketslots.ClientTask;
import ticketslots.ProtocolData;
import ticketslots.TaskContext;
import ticketslots.ui.UiText;

public final class AuthTask extends ClientTask {
    private String pendingUsername = "";
    private char[] pendingPassword;
    private boolean requestInFlight;

    @Override
    public String id() {
        return "auth";
    }

    @Override
    public boolean accepts(AppEvent event) {
        if (AppEvent.REGISTER_REQUEST.equals(event.type())
                || AppEvent.LOGIN_REQUEST.equals(event.type())
                || AppEvent.LOGOUT_REQUEST.equals(event.type())
                || AppEvent.WHOAMI_REQUEST.equals(event.type())) {
            return true;
        }
        if (AppEvent.NETWORK_DISCONNECTED.equals(event.type())) {
            return true;
        }
        if (AppEvent.REQUEST_TIMEOUT.equals(event.type())) {
            return event.get("operation").startsWith("auth.");
        }
        return AppEvent.SERVER_RESPONSE.equals(event.type())
                && event.get("operation").startsWith("auth.");
    }

    @Override
    public void handle(AppEvent event, TaskContext context) {
        switch (event.type()) {
            case AppEvent.REGISTER_REQUEST -> register(event, context);
            case AppEvent.LOGIN_REQUEST -> beginLogin(event, context);
            case AppEvent.LOGOUT_REQUEST -> logout(context);
            case AppEvent.WHOAMI_REQUEST -> whoami(context);
            case AppEvent.NETWORK_DISCONNECTED -> resetRequest();
            case AppEvent.REQUEST_TIMEOUT -> authTimeout(event, context);
            case AppEvent.SERVER_RESPONSE -> handleResponse(event, context);
            default -> {
            }
        }
    }

    private void register(AppEvent event, TaskContext context) {
        if (requestInFlight) {
            context.ui().log("[认证] 已有认证请求正在处理");
            return;
        }
        String username = event.get("username").trim();
        char[] password = passwordOf(event);
        if (!validateCredentials(username, password, context)) {
            clear(password);
            return;
        }
        String payload;
        try {
            payload = "register|" + username + "|" + new String(password);
        } finally {
            clear(password);
        }
        context.ui().setBusy("正在注册账号 " + username);
        requestInFlight = true;
        context.protocol().request("auth.register", payload, "register", "");
        context.ui().log("[认证] 注册请求已提交");
    }

    private void beginLogin(AppEvent event, TaskContext context) {
        if (requestInFlight) {
            context.ui().log("[认证] 登录请求正在处理");
            return;
        }
        if (!context.state().connected()) {
            context.ui().log("[认证] 请先连接服务器再登录");
            return;
        }
        String username = event.get("username").trim();
        char[] password = passwordOf(event);
        if (!validateCredentials(username, password, context)) {
            clear(password);
            return;
        }
        clearPending();
        pendingUsername = username;
        pendingPassword = password;
        context.ui().setBusy("正在请求登录挑战");
        requestInFlight = true;
        context.protocol().request("auth.challenge",
                "login.challenge|" + username, "login.challenge", "");
        context.ui().log("[认证] 登录挑战请求已提交");
    }

    private void logout(TaskContext context) {
        if (requestInFlight) {
            context.ui().log("[认证] 请等待当前认证请求完成");
            return;
        }
        if (!context.state().authenticated()) {
            context.ui().log("[认证] 当前没有已登录的会话");
            return;
        }
        context.ui().setBusy("正在退出登录");
        requestInFlight = true;
        context.protocol().request("auth.logout", "logout|", "logout", "");
    }

    private void whoami(TaskContext context) {
        if (requestInFlight) {
            context.ui().log("[认证] 请等待当前认证请求完成");
            return;
        }
        if (!context.state().connected()) {
            context.ui().log("[认证] 请先连接服务器再查询身份");
            return;
        }
        requestInFlight = true;
        context.protocol().request("auth.whoami", "whoami|", "whoami", "");
    }

    private void handleResponse(AppEvent event, TaskContext context) {
        String operation = event.get("operation");
        String[] parts = ProtocolData.header(event);
        boolean ok = event.getBoolean("ok", false);

        if ("auth.register".equals(operation)) {
            requestInFlight = false;
            context.ui().setBusy("");
            if (ok) {
                context.state().updateTask("auth", "REGISTERED",
                        ProtocolData.part(parts, 2), "");
                context.ui().log("[认证] 注册成功，用户名："
                        + ProtocolData.part(parts, 2));
                context.navigation().go("login", "auth.register");
            } else {
                context.state().updateTask("auth", "REGISTER_FAILED", "",
                        UiText.errorDetail(parts));
                context.ui().log("[认证] 注册失败："
                        + UiText.errorDetail(parts));
            }
            return;
        }

        if ("auth.challenge".equals(operation)) {
            if (!ok) {
                requestInFlight = false;
                clearPending();
                context.ui().setBusy("");
                context.state().sessionFailed("challenge failed");
                context.ui().log("[认证] 登录挑战失败："
                        + UiText.errorDetail(parts));
                return;
            }
            challengeProof(parts, context);
            return;
        }

        if ("auth.proof".equals(operation)) {
            requestInFlight = false;
            context.ui().setBusy("");
            if (ok && parts.length >= 4) {
                context.state().startSession(pendingUsername,
                        parts[3], parts[2]);
                context.state().updateTask("auth", "SIGNED_IN",
                        parts[3], "用户 " + pendingUsername);
                pendingUsername = "";
                context.navigation().go("catalog", "auth.login");
                context.ui().log("[认证] 已登录，用户编号 #" + parts[3]);
                context.eventBus().submit(AppEvent.of(AppEvent.CATALOG_REQUEST));
            } else {
                context.state().sessionFailed("login failed");
                context.state().updateTask("auth", "LOGIN_FAILED", "",
                        UiText.errorDetail(parts));
                context.ui().log("[认证] 登录失败："
                        + UiText.errorDetail(parts));
            }
            return;
        }

        if ("auth.logout".equals(operation)) {
            requestInFlight = false;
            context.ui().setBusy("");
            context.state().clearSession(ok ? "signed out" : "logout failed");
            context.state().updateTask("auth", ok ? "SIGNED_OUT" : "LOGOUT_FAILED",
                    "", "");
            context.navigation().go("login", "auth.logout");
            context.ui().log(ok ? "[认证] 已退出登录"
                    : "[认证] 退出登录失败：" + UiText.errorDetail(parts));
            return;
        }

        if ("auth.whoami".equals(operation)) {
            requestInFlight = false;
            context.ui().setBusy("");
            if (ok) {
                context.state().updateTask("auth", "IDENTITY",
                        ProtocolData.part(parts, 4),
                        ProtocolData.part(parts, 3));
                context.ui().log("[认证] 用户编号 " + ProtocolData.part(parts, 2)
                        + "，用户名 " + ProtocolData.part(parts, 3)
                        + "，虚拟连接 " + ProtocolData.part(parts, 4));
            } else {
                context.state().updateTask("auth", "IDENTITY_FAILED", "",
                        UiText.errorDetail(parts));
                context.ui().log("[认证] 身份查询失败："
                        + UiText.errorDetail(parts));
            }
        }
    }

    private void challengeProof(String[] parts, TaskContext context) {
        if (pendingPassword == null || pendingUsername.isEmpty()) {
            context.ui().log("[认证] 收到挑战响应，但没有待处理的登录请求");
            return;
        }
        String nonce = ProtocolData.part(parts, 2);
        String salt = ProtocolData.part(parts, 3);
        String username = pendingUsername;
        String proof;
        try {
            context.ui().setBusy("正在计算登录凭证");
            proof = AuthCrypto.challengeProof(pendingPassword, salt, nonce);
        } catch (GeneralSecurityException | IllegalArgumentException error) {
            clearPending();
            context.ui().setBusy("");
            context.ui().log("[认证] 登录凭证计算失败："
                    + UiText.reason(error.getMessage()));
            return;
        } finally {
            clear(pendingPassword);
            pendingPassword = null;
        }
        context.protocol().request("auth.proof",
                "login.proof|" + username + "|" + proof,
                "login.proof", "");
    }

    private static boolean validateCredentials(String username, char[] password,
                                               TaskContext context) {
        if (username.length() < 3 || username.length() > 32) {
            context.ui().log("[认证] 用户名长度必须为 3 至 32 个字符");
            return false;
        }
        if (password == null || password.length < 6 || password.length > 64) {
            context.ui().log("[认证] 密码长度必须为 6 至 64 个字符");
            return false;
        }
        if (!context.state().connected()) {
            context.ui().log("[认证] 请先连接服务器再进行认证");
            return false;
        }
        return true;
    }

    private static char[] passwordOf(AppEvent event) {
        Object value = event.value("password");
        if (value instanceof char[] chars) {
            return chars;
        }
        return value == null ? new char[0] : String.valueOf(value).toCharArray();
    }

    private void clearPending() {
        pendingUsername = "";
        clear(pendingPassword);
        pendingPassword = null;
    }

    private void resetRequest() {
        requestInFlight = false;
        clearPending();
    }

    private void authTimeout(AppEvent event, TaskContext context) {
        resetRequest();
        context.ui().setBusy("");
        if ("auth.proof".equals(event.get("operation"))) {
            context.state().sessionFailed("login timed out");
            context.state().updateTask("auth", "TIMEOUT", "", "请求超时");
        }
        context.ui().log("[认证] 请求超时："
                + UiText.operationName(event.get("operation")));
    }

    private static void clear(char[] value) {
        if (value != null) {
            Arrays.fill(value, '\0');
        }
    }
}
