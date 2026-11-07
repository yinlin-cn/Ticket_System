package ticketslots.ui;

import java.awt.BorderLayout;
import java.awt.FlowLayout;
import java.awt.Font;
import javax.swing.JButton;
import javax.swing.JPanel;
import javax.swing.JScrollPane;
import javax.swing.JTextArea;

final class LogScreen implements ScreenView {
    private final JPanel root;
    private final JTextArea log = new JTextArea();

    LogScreen() {
        root = TemplateLibrary.page("活动记录",
                "按到达顺序显示协议、任务和网络事件。");

        log.setEditable(false);
        log.setFont(new Font("Microsoft YaHei UI", Font.PLAIN, 12));
        log.setBackground(TemplateLibrary.PANEL);
        log.setForeground(TemplateLibrary.INK);
        log.setBorder(TemplateLibrary.sectionBorder());

        JButton clear = TemplateLibrary.secondaryButton("清空记录");
        clear.addActionListener(event -> log.setText(""));
        JPanel toolbar = new JPanel(new FlowLayout(FlowLayout.LEFT, 0, 0));
        toolbar.setOpaque(false);
        toolbar.add(clear);

        JPanel content = new JPanel(new BorderLayout(0, 10));
        content.setOpaque(false);
        content.add(toolbar, BorderLayout.NORTH);
        content.add(new JScrollPane(log), BorderLayout.CENTER);
        root.add(content, BorderLayout.CENTER);
    }

    void append(String line) {
        log.append(line + "\n");
        log.setCaretPosition(log.getDocument().getLength());
    }

    @Override
    public String id() {
        return "log";
    }

    @Override
    public JPanel component() {
        return root;
    }
}
