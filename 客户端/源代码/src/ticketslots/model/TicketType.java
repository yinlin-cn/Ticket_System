package ticketslots.model;

import ticketslots.AppEvent;

public record TicketType(
        String id,
        String eventName,
        String ticketName,
        long priceCents,
        int remaining) {
    public static TicketType fromEvent(AppEvent event) {
        return new TicketType(
                event.get("type"),
                event.get("event"),
                event.get("ticket"),
                event.getLong("price", 0),
                event.getInt("remaining", 0));
    }
}
