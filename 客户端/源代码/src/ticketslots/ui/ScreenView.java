package ticketslots.ui;

import javax.swing.JComponent;

interface ScreenView {
    String id();

    JComponent component();

    default void activate() {
    }
}
