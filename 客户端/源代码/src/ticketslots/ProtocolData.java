package ticketslots;

public final class ProtocolData {
    private ProtocolData() {
    }

    public static String[] header(AppEvent response) {
        String payload = response.get("payload");
        int newline = payload.indexOf('\n');
        String head = newline < 0 ? payload : payload.substring(0, newline);
        return head.split("\\|", -1);
    }

    public static String[] lines(AppEvent response) {
        return response.get("payload").split("\\n", -1);
    }

    public static String part(String[] parts, int index) {
        return index >= 0 && index < parts.length ? parts[index] : "";
    }

    public static boolean ok(String[] parts) {
        return parts.length > 1 && "ok".equals(parts[1]);
    }

    public static String errorDetail(String[] parts) {
        if (parts.length < 3) {
            return "unknown server error";
        }
        String code = part(parts, 2);
        String detail = part(parts, 3);
        return detail.isEmpty() ? code : code + ": " + detail;
    }
}
