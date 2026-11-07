package ticketslots.ui;

import java.util.Locale;
import java.util.Map;

/**
 * Chinese presentation text for protocol and task values.
 *
 * <p>Protocol commands, event names and database states remain unchanged.
 * Only text shown to the user is translated here.</p>
 */
public final class UiText {
    private static final Map<String, String> TASK_STATUS = Map.ofEntries(
            Map.entry("IDLE", "空闲"),
            Map.entry("CONNECTED", "已连接"),
            Map.entry("DISCONNECTED", "连接已断开"),
            Map.entry("LOADING", "加载中"),
            Map.entry("LOADED", "已加载"),
            Map.entry("QUEUING", "正在排队"),
            Map.entry("QUEUED", "已进入队列"),
            Map.entry("REJECTED", "已拒绝"),
            Map.entry("TIMEOUT", "已超时"),
            Map.entry("SIGNED_IN", "已登录"),
            Map.entry("SIGNED_OUT", "已退出登录"),
            Map.entry("REGISTERED", "注册成功"),
            Map.entry("REGISTER_FAILED", "注册失败"),
            Map.entry("LOGIN_FAILED", "登录失败"),
            Map.entry("LOGOUT_FAILED", "退出失败"),
            Map.entry("IDENTITY", "身份信息"),
            Map.entry("IDENTITY_FAILED", "身份查询失败"),
            Map.entry("READY", "就绪"),
            Map.entry("ERROR", "出错"),
            Map.entry("LOOKING_UP", "查询中"),
            Map.entry("NAVIGATED", "已切换页面"),
            Map.entry("PENDING_PAY", "待支付"),
            Map.entry("PAID", "已支付"),
            Map.entry("PARTIALLY_REFUNDED", "部分退款"),
            Map.entry("REFUNDED", "已退款"),
            Map.entry("CANCELLED", "已取消"),
            Map.entry("EXPIRED", "已过期"));

    private static final Map<String, String> ERROR_CODE = Map.ofEntries(
            Map.entry("CLIENT_SEND_FAILED", "网络未连接，消息发送失败"),
            Map.entry("BAD_USERNAME", "用户名长度必须为 3 至 32 个字符"),
            Map.entry("BAD_PASSWORD", "密码长度必须为 6 至 64 个字符"),
            Map.entry("DB_ERROR", "数据库暂不可用"),
            Map.entry("USER_EXISTS", "用户名已存在"),
            Map.entry("CRYPTO_ERROR", "密码加密处理失败"),
            Map.entry("BAD_CREDENTIALS", "用户名或密码错误"),
            Map.entry("ACCOUNT_LOCKED", "账户暂时锁定，请稍后再试"),
            Map.entry("BAD_USER", "用户记录无效"),
            Map.entry("NO_CONNECTION", "连接尚未注册"),
            Map.entry("CHALLENGE_EXPIRED", "登录挑战已过期，请重新登录"),
            Map.entry("BAD_PROOF", "登录凭证校验失败"),
            Map.entry("UNAUTHORIZED", "请先登录"),
            Map.entry("BAD_IDEMPOTENCY_KEY", "请求标识无效"),
            Map.entry("BAD_TICKET_TYPE", "票种无效"),
            Map.entry("BAD_QUANTITY", "数量无效"),
            Map.entry("BAD_ORDER", "订单号无效"),
            Map.entry("NOT_FOUND", "未找到相关记录"),
            Map.entry("TOKEN_ERROR", "登录令牌无效，请重新登录"),
            Map.entry("ALREADY_ONLINE", "该账户已在其他连接登录"),
            Map.entry("REGISTER_FAILED", "连接注册失败"),
            Map.entry("SERVER_BUSY", "服务器繁忙，请稍后重试"),
            Map.entry("DUPLICATE", "请求已处理"),
            Map.entry("NOT_OWNER", "无权操作该订单"),
            Map.entry("TICKET_TYPE_MISMATCH", "票种与订单不匹配"),
            Map.entry("STATUS_INVALID", "当前订单状态不允许此操作"),
            Map.entry("EXCEED_REFUNDABLE", "退票数量超过可退数量"),
            Map.entry("BAD_PRICE", "票价信息不可用"),
            Map.entry("SOLD_OUT", "余票不足"),
            Map.entry("NOT_LOGGED_IN", "当前连接尚未登录"),
            Map.entry("BAD_TARGET", "目标用户无效"),
            Map.entry("BAD_REQUEST", "请求格式错误"),
            Map.entry("UNKNOWN_COMMAND", "未知协议命令"));

    private static final Map<String, String> SCREEN = Map.ofEntries(
            Map.entry("connection", "服务器连接"),
            Map.entry("login", "账户登录"),
            Map.entry("catalog", "票务目录"),
            Map.entry("buy", "确认购票"),
            Map.entry("orders", "我的订单"),
            Map.entry("refund", "申请退票"),
            Map.entry("log", "活动记录"));

    private static final Map<String, String> SOURCE = Map.ofEntries(
            Map.entry("event", "事件"),
            Map.entry("sidebar", "侧栏"),
            Map.entry("self-test", "自测"),
            Map.entry("navigation-test", "导航测试"),
            Map.entry("connection.closed", "连接关闭"),
            Map.entry("connection.failed", "连接失败"),
            Map.entry("auth.register", "注册完成"),
            Map.entry("auth.login", "登录成功"),
            Map.entry("auth.logout", "退出登录"),
            Map.entry("catalog.select", "选择票种"),
            Map.entry("orders.select", "选择订单"));

    private static final Map<String, String> OPERATION = Map.ofEntries(
            Map.entry("auth.register", "账户注册"),
            Map.entry("auth.challenge", "登录挑战"),
            Map.entry("auth.proof", "登录凭证校验"),
            Map.entry("auth.logout", "退出登录"),
            Map.entry("auth.whoami", "身份查询"),
            Map.entry("catalog.list", "票务目录查询"),
            Map.entry("reserve", "提交购票"),
            Map.entry("reserve.result", "购票结果"),
            Map.entry("orders.list", "订单列表查询"),
            Map.entry("order.status", "订单状态查询"),
            Map.entry("refund", "提交退票"),
            Map.entry("refund.result", "退票结果"));

    private UiText() {
    }

    public static String taskStatus(String status) {
        if (status == null || status.isBlank()) {
            return "空闲";
        }
        String value = status.trim();
        String upper = value.toUpperCase(Locale.ROOT);
        if (upper.startsWith("REJECTED ")) {
            String code = value.substring("REJECTED ".length()).trim();
            String reason = ERROR_CODE.get(code);
            return reason == null ? "已拒绝（" + code + "）" : "已拒绝：" + reason;
        }
        return TASK_STATUS.getOrDefault(upper, value);
    }

    public static String orderStatus(String status) {
        if (status == null || status.isBlank()) {
            return "";
        }
        String value = status.trim();
        return TASK_STATUS.getOrDefault(value.toUpperCase(Locale.ROOT), value);
    }

    public static String detail(String value) {
        if (value == null || value.isBlank()) {
            return "";
        }
        String text = value.trim();
        String lower = text.toLowerCase(Locale.ROOT);
        if (lower.startsWith("amount ")) {
            return "金额 " + text.substring("amount ".length());
        }
        if (lower.startsWith("quantity ")) {
            return "数量 " + text.substring("quantity ".length()) + " 张";
        }
        if (lower.startsWith("stock ")) {
            return "可售余量 " + text.substring("stock ".length());
        }
        if (lower.startsWith("user ")) {
            return "用户 " + text.substring("user ".length());
        }
        if (lower.equals("unknown destination")) {
            return "目标页面不存在";
        }
        String translated = reason(text);
        return translated.isEmpty() ? text : translated;
    }

    public static String operationName(String operation) {
        if (operation == null || operation.isBlank()) {
            return "";
        }
        return OPERATION.getOrDefault(operation, operation);
    }

    public static String reason(String value) {
        if (value == null || value.isBlank()) {
            return "";
        }
        String text = value.trim();
        String translated = switch (text.toLowerCase(Locale.ROOT)) {
            case "offline" -> "未连接";
            case "connected" -> "已连接";
            case "authenticated" -> "已登录";
            case "not signed in" -> "未登录";
            case "connection failed" -> "连接失败";
            case "authentication failed" -> "认证失败";
            case "login failed" -> "登录失败";
            case "challenge failed" -> "挑战请求失败";
            case "login timed out" -> "登录超时";
            case "signed out" -> "已退出登录";
            case "logout failed" -> "退出登录失败";
            case "connection closed" -> "连接已关闭";
            case "server closed the connection" -> "服务器关闭了连接";
            case "read failed" -> "读取数据失败";
            case "write failed" -> "发送数据失败";
            case "client closed" -> "客户端已关闭";
            case "reconnect" -> "重新连接";
            case "closed from client" -> "客户端主动断开";
            case "request timed out" -> "请求超时";
            case "network is not connected" -> "网络未连接";
            case "login required" -> "请先登录";
            case "order not found" -> "未找到订单";
            case "database unavailable" -> "数据库暂不可用";
            case "connection refused" -> "服务器拒绝连接";
            case "connection timed out" -> "连接超时";
            case "outgoing frame is empty or exceeds 9999 bytes" ->
                    "待发送的数据为空或超过 9999 字节";
            case "connection is already open" -> "网络连接已经建立";
            default -> "";
        };
        if (!translated.isEmpty()) {
            return translated;
        }
        String lower = text.toLowerCase(Locale.ROOT);
        if (lower.startsWith("write failed:")) {
            return "发送数据失败：" + reason(text.substring("write failed:".length()));
        }
        if (lower.startsWith("connection closed inside")) {
            return "数据帧传输中断";
        }
        if (lower.startsWith("bad frame header")) {
            return "数据帧头格式无效";
        }
        if (lower.startsWith("invalid frame length")
                || lower.startsWith("bad frame length")) {
            return "数据帧长度无效";
        }
        if (lower.contains("connection refused")) {
            return "服务器拒绝连接";
        }
        if (lower.contains("timed out")) {
            return "连接超时";
        }
        return text;
    }

    public static String errorDetail(String[] parts) {
        String code = part(parts, 2);
        String detail = part(parts, 3);
        return error(code, detail);
    }

    public static String error(String code, String detail) {
        String safeCode = code == null ? "" : code.trim();
        String safeDetail = detail == null ? "" : detail.trim();
        String known = ERROR_CODE.get(safeCode);
        if (known != null) {
            return safeCode.isEmpty() ? known : known + "（错误码 " + safeCode + "）";
        }
        if (!safeDetail.isEmpty()) {
            String translated = reason(safeDetail);
            return safeCode.isEmpty()
                    ? translated
                    : translated + "（错误码 " + safeCode + "）";
        }
        return safeCode.isEmpty()
                ? "服务器返回未知错误"
                : "服务器返回错误（错误码 " + safeCode + "）";
    }

    public static String ticketTypes(int count) {
        return count + " 个票种";
    }

    public static String orders(int count) {
        return count + " 笔订单";
    }

    public static String tickets(int count) {
        return count + " 张票";
    }

    public static String screenName(String screen) {
        return SCREEN.getOrDefault(screen == null ? "" : screen, screen);
    }

    public static String sourceName(String source) {
        return SOURCE.getOrDefault(source == null ? "" : source, source);
    }

    private static String part(String[] parts, int index) {
        return parts != null && index >= 0 && index < parts.length ? parts[index] : "";
    }
}
