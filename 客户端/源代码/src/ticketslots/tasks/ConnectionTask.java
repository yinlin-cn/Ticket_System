package ticketslots.tasks;

import java.io.IOException;
import ticketslots.AppEvent;
import ticketslots.ClientTask;
import ticketslots.ProtocolData;
import ticketslots.TaskContext;
import ticketslots.ui.UiText;

public final class ConnectionTask extends ClientTask {
    @Override
    public String id() {
        return "connection";
    }

    @Override
    public boolean accepts(AppEvent event) {
        if (AppEvent.CONNECT_REQUEST.equals(event.type())
                || AppEvent.DISCONNECT_REQUEST.equals(event.type())
                || AppEvent.NETWORK_CONNECTED.equals(event.type())
                || AppEvent.NETWORK_DISCONNECTED.equals(event.type())
                || AppEvent.NETWORK_ERROR.equals(event.type())
                || AppEvent.PING_REQUEST.equals(event.type())) {
            return true;
        }
        return AppEvent.SERVER_RESPONSE.equals(event.type())
                && "push".equals(event.get("operation"));
    }

    @Override
    public void handle(AppEvent event, TaskContext context) {
        switch (event.type()) {
            case AppEvent.CONNECT_REQUEST -> connect(event, context);
            case AppEvent.DISCONNECT_REQUEST ->
                    context.network().disconnect("closed from client");
            case AppEvent.NETWORK_CONNECTED -> {
                context.state().connectionOpen(event.get("host"),
                        event.getInt("port", 9101));
                context.state().updateTask("connection", "CONNECTED",
                        event.get("host") + ":" + event.get("port"), "");
                context.ui().log("[网络] 已连接到 "
                        + event.get("host") + ":" + event.get("port"));
            }
            case AppEvent.NETWORK_DISCONNECTED -> {
                context.state().connectionClosed(event.get("reason"));
                context.navigation().go("connection", "connection.closed");
                context.protocol().resetPending("connection closed");
                context.ui().setBusy("");
                context.ui().log("[网络] " + UiText.reason(event.get("reason")));
            }
            case AppEvent.NETWORK_ERROR ->
                    context.ui().log("[网络] " + UiText.reason(event.get("detail")));
            case AppEvent.PING_REQUEST ->
                    context.network().send("ping|");
            case AppEvent.SERVER_RESPONSE -> handlePush(event, context);
            default -> {
            }
        }
    }

    private void connect(AppEvent event, TaskContext context) {
        if (context.network().isConnected()) {
            context.ui().log("[网络] 网络连接已经建立");
            return;
        }
        String host = event.get("host", "127.0.0.1");
        int port = event.getInt("port", 9101);
        if (port < 1 || port > 65535) {
            context.ui().log("[网络] 端口必须在 1 到 65535 之间");
            return;
        }
        context.ui().setBusy("正在连接 " + host + ":" + port);
        try {
            context.network().connect(host, port);
        } catch (IOException error) {
            context.ui().setBusy("");
            context.state().connectionFailed(error.getMessage());
            context.navigation().go("connection", "connection.failed");
            context.ui().log("[网络] 连接失败："
                    + UiText.reason(error.getMessage()));
        }
    }

    private void handlePush(AppEvent event, TaskContext context) {
        String command = event.get("command");
        if ("pong".equals(command)) {
            return;
        }
        if ("notice".equals(command)) {
            String[] parts = ProtocolData.header(event);
            context.ui().log("[通知] 来自 " + ProtocolData.part(parts, 1)
                    + ": " + ProtocolData.part(parts, 2));
            return;
        }
        context.ui().log("[服务器] " + event.get("payload").replace('\n', ' '));
    }
}
