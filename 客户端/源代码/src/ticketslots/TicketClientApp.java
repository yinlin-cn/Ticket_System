package ticketslots;

import java.awt.Font;
import java.util.concurrent.atomic.AtomicBoolean;
import javax.swing.SwingUtilities;
import javax.swing.UIManager;
import ticketslots.tasks.AuthTask;
import ticketslots.tasks.CatalogTask;
import ticketslots.tasks.ConnectionTask;
import ticketslots.tasks.NavigationTask;
import ticketslots.tasks.OrderTask;
import ticketslots.tasks.RefundTask;
import ticketslots.tasks.ReserveTask;
import ticketslots.ui.MainFrame;
import ticketslots.ui.SwingUiPort;

public final class TicketClientApp {
    private TicketClientApp() {
    }

    public static void main(String[] args) {
        SwingUtilities.invokeLater(TicketClientApp::launch);
    }

    private static void launch() {
        configureLookAndFeel();

        MainFrame frame = new MainFrame();
        ComponentWorker components = new ComponentWorker();
        SwingUiPort ui = new SwingUiPort(frame, components);
        AppState state = new AppState();
        NavigationService navigation = new NavigationService(state);
        LongTaskRuntime runtime = new LongTaskRuntime(ui);
        NetworkClient network = new NetworkClient(runtime);
        ProtocolWorker protocol = new ProtocolWorker(network, runtime);
        TaskContext context = new TaskContext(
                state, navigation, network, protocol, ui, runtime);

        runtime.configure(context, protocol);
        runtime.addTask(new NavigationTask());
        runtime.addTask(new ConnectionTask());
        runtime.addTask(new AuthTask());
        runtime.addTask(new CatalogTask());
        runtime.addTask(new ReserveTask());
        runtime.addTask(new OrderTask());
        runtime.addTask(new RefundTask());

        AtomicBoolean closing = new AtomicBoolean();
        frame.attach(runtime);
        frame.setCloseAction(() -> {
            if (!closing.compareAndSet(false, true)) {
                return;
            }
            frame.setEnabled(false);
            runtime.close();
            network.close();
            protocol.close();
            components.close();
            frame.dispose();
        });

        runtime.start();
        frame.setVisible(true);
    }

    private static void configureLookAndFeel() {
        try {
            UIManager.setLookAndFeel(UIManager.getSystemLookAndFeelClassName());
        } catch (Exception ignored) {
            // The default Swing look and feel remains usable.
        }
        Font base = new Font("Microsoft YaHei UI", Font.PLAIN, 13);
        UIManager.put("Label.font", base);
        UIManager.put("Button.font", base);
        UIManager.put("TextField.font", base);
        UIManager.put("PasswordField.font", base);
        UIManager.put("Spinner.font", base);
        UIManager.put("Table.font", base);
        UIManager.put("TableHeader.font", base.deriveFont(Font.BOLD));
    }
}
