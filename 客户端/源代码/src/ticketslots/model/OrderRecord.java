package ticketslots.model;

import ticketslots.AppEvent;

public record OrderRecord(
        String orderNo,
        String ticketType,
        int quantity,
        long amountCents,
        String status,
        int refundedQuantity,
        String createdAt) {
    public static OrderRecord fromEvent(AppEvent event) {
        return new OrderRecord(
                event.get("order"),
                event.get("type"),
                event.getInt("quantity", 0),
                event.getLong("amount", 0),
                event.get("status"),
                event.getInt("refunded", 0),
                event.get("created"));
    }
}
