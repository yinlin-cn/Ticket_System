package ticketslots.ui;

import java.awt.BorderLayout;
import java.awt.event.MouseAdapter;
import java.awt.event.MouseEvent;
import java.util.ArrayList;
import java.util.List;
import javax.swing.JButton;
import javax.swing.JLabel;
import javax.swing.JPanel;
import javax.swing.JScrollPane;
import javax.swing.JTable;
import javax.swing.ListSelectionModel;
import javax.swing.table.DefaultTableModel;
import ticketslots.AppEvent;
import ticketslots.FrontendSlot;
import ticketslots.model.TicketType;

final class CatalogScreen implements ScreenView {
    private final FrontendSlot slot;
    private final JPanel root;
    private final DefaultTableModel model = new DefaultTableModel(
            new Object[] {"票种编号", "活动", "票种", "票价", "余票"}, 0) {
        @Override
        public boolean isCellEditable(int row, int column) {
            return false;
        }
    };
    private final JTable table = new JTable(model);
    private final JButton refresh = TemplateLibrary.secondaryButton("刷新");
    private final JButton purchase = TemplateLibrary.primaryButton("进入购票");
    private final JLabel status = new JLabel("尚未加载票务目录");
    private final List<TicketType> tickets = new ArrayList<>();

    CatalogScreen(FrontendSlot slot) {
        this.slot = slot;
        root = TemplateLibrary.page("票务目录",
                "查看实时余票，并选择票种进入购票流程。");

        JPanel toolbar = TemplateLibrary.toolbar();
        toolbar.add(refresh);
        toolbar.add(purchase);
        toolbar.add(status);

        table.setFillsViewportHeight(true);
        table.setRowHeight(26);
        table.setSelectionMode(ListSelectionModel.SINGLE_SELECTION);
        table.getTableHeader().setReorderingAllowed(false);
        JScrollPane scroll = new JScrollPane(table);
        scroll.setBorder(TemplateLibrary.sectionBorder());

        JPanel content = new JPanel(new BorderLayout(0, 12));
        content.setOpaque(false);
        content.add(toolbar, BorderLayout.NORTH);
        content.add(scroll, BorderLayout.CENTER);
        root.add(content, BorderLayout.CENTER);

        refresh.addActionListener(event ->
                slot.submit(AppEvent.of(AppEvent.CATALOG_REQUEST)));
        purchase.addActionListener(event -> openSelected());
        table.addMouseListener(new MouseAdapter() {
            @Override
            public void mouseClicked(MouseEvent event) {
                if (event.getClickCount() == 2) {
                    openSelected();
                }
            }
        });
    }

    void setTickets(List<TicketType> values) {
        tickets.clear();
        tickets.addAll(values);
        model.setRowCount(0);
        for (TicketType ticket : values) {
            model.addRow(new Object[] {
                    ticket.id(), ticket.eventName(), ticket.ticketName(),
                    money(ticket.priceCents()), ticket.remaining()
            });
        }
        status.setText(UiText.ticketTypes(values.size()));
    }

    private void openSelected() {
        int row = table.getSelectedRow();
        if (row < 0 || row >= tickets.size()) {
            status.setText("请先选择票种");
            return;
        }
        slot.submit(AppEvent.of(AppEvent.TICKET_SELECTED, "ticket", tickets.get(row)));
    }

    private static String money(long cents) {
        return String.format("%d.%02d", cents / 100, Math.abs(cents % 100));
    }

    @Override
    public String id() {
        return "catalog";
    }

    @Override
    public JPanel component() {
        return root;
    }
}
