package ticketslots;

public record TaskContext(
        AppState state,
        NavigationService navigation,
        NetworkClient network,
        ProtocolWorker protocol,
        UiPort ui,
        FrontendSlot eventBus) {
}
