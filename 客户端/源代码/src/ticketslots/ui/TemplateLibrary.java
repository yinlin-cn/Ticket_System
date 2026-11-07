package ticketslots.ui;

import java.awt.BorderLayout;
import java.awt.Color;
import java.awt.Component;
import java.awt.Dimension;
import java.awt.FlowLayout;
import java.awt.Font;
import java.awt.GridBagConstraints;
import java.awt.GridBagLayout;
import java.awt.GridLayout;
import java.awt.Insets;
import javax.swing.BorderFactory;
import javax.swing.JButton;
import javax.swing.JComponent;
import javax.swing.JLabel;
import javax.swing.JPanel;
import javax.swing.border.Border;
import javax.swing.plaf.basic.BasicButtonUI;

/** Reusable visual templates; screens contain business wiring, not drawing code. */
final class TemplateLibrary {
    static final Color INK = new Color(31, 41, 51);
    static final Color MUTED = new Color(96, 112, 128);
    static final Color CANVAS = new Color(244, 247, 246);
    static final Color PANEL = Color.WHITE;
    static final Color ACCENT = new Color(13, 116, 106);
    static final Color WARNING = new Color(180, 83, 9);
    static final Color BORDER = new Color(216, 224, 222);

    private TemplateLibrary() {
    }

    static JPanel page(String title, String description) {
        JPanel page = new JPanel(new BorderLayout(0, 18));
        page.setBackground(CANVAS);
        page.setBorder(BorderFactory.createEmptyBorder(26, 30, 26, 30));

        JPanel heading = new JPanel(new BorderLayout(0, 5));
        heading.setOpaque(false);
        JLabel titleLabel = new JLabel(title);
        titleLabel.setForeground(INK);
        titleLabel.setFont(titleLabel.getFont().deriveFont(Font.BOLD, 24f));
        JLabel descriptionLabel = new JLabel(description);
        descriptionLabel.setForeground(MUTED);
        descriptionLabel.setFont(descriptionLabel.getFont().deriveFont(13f));
        heading.add(titleLabel, BorderLayout.NORTH);
        heading.add(descriptionLabel, BorderLayout.CENTER);
        page.add(heading, BorderLayout.NORTH);
        return page;
    }

    static JPanel surface() {
        JPanel panel = new JPanel(new GridBagLayout());
        panel.setBackground(PANEL);
        panel.setBorder(BorderFactory.createCompoundBorder(
                BorderFactory.createLineBorder(BORDER),
                BorderFactory.createEmptyBorder(20, 22, 20, 22)));
        return panel;
    }

    static JPanel toolbar() {
        JPanel panel = new JPanel(new FlowLayout(FlowLayout.LEFT, 8, 0));
        panel.setOpaque(false);
        return panel;
    }

    static void addFormRow(JPanel form, int row, String label, Component component) {
        GridBagConstraints labelConstraints = new GridBagConstraints();
        labelConstraints.gridx = 0;
        labelConstraints.gridy = row;
        labelConstraints.anchor = GridBagConstraints.LINE_START;
        labelConstraints.insets = new Insets(6, 0, 6, 18);

        JLabel fieldLabel = new JLabel(label);
        fieldLabel.setForeground(MUTED);
        form.add(fieldLabel, labelConstraints);

        GridBagConstraints fieldConstraints = new GridBagConstraints();
        fieldConstraints.gridx = 1;
        fieldConstraints.gridy = row;
        fieldConstraints.weightx = 1;
        fieldConstraints.fill = GridBagConstraints.HORIZONTAL;
        fieldConstraints.insets = new Insets(6, 0, 6, 0);
        form.add(component, fieldConstraints);
    }

    static JButton primaryButton(String text) {
        JButton button = new JButton(text);
        button.setBackground(ACCENT);
        button.setForeground(Color.WHITE);
        button.setBorder(BorderFactory.createEmptyBorder(9, 16, 9, 16));
        flatButton(button);
        return button;
    }

    static JButton secondaryButton(String text) {
        JButton button = new JButton(text);
        button.setBackground(PANEL);
        button.setForeground(INK);
        button.setBorder(BorderFactory.createCompoundBorder(
                BorderFactory.createLineBorder(BORDER),
                BorderFactory.createEmptyBorder(8, 14, 8, 14)));
        flatButton(button);
        return button;
    }

    static void flatButton(JButton button) {
        button.setUI(new BasicButtonUI());
        button.setOpaque(true);
        button.setContentAreaFilled(true);
        button.setFocusPainted(false);
        button.setRolloverEnabled(true);
        button.setCursor(java.awt.Cursor.getPredefinedCursor(
                java.awt.Cursor.HAND_CURSOR));
    }

    static JPanel summary(String caption, JLabel value) {
        JPanel panel = new JPanel(new BorderLayout(0, 3));
        panel.setOpaque(false);
        JLabel captionLabel = new JLabel(caption);
        captionLabel.setForeground(MUTED);
        captionLabel.setFont(captionLabel.getFont().deriveFont(12f));
        value.setForeground(INK);
        value.setFont(value.getFont().deriveFont(Font.BOLD, 16f));
        panel.add(captionLabel, BorderLayout.NORTH);
        panel.add(value, BorderLayout.CENTER);
        panel.setPreferredSize(new Dimension(1, 48));
        return panel;
    }

    static JPanel summaryStrip(JPanel... items) {
        JPanel strip = new JPanel(new GridLayout(1, items.length, 18, 0));
        strip.setOpaque(false);
        for (JPanel item : items) {
            strip.add(item);
        }
        return strip;
    }

    static Border sectionBorder() {
        return BorderFactory.createCompoundBorder(
                BorderFactory.createLineBorder(BORDER),
                BorderFactory.createEmptyBorder(16, 18, 16, 18));
    }

    static void fixedHeight(JComponent component, int height) {
        Dimension size = component.getPreferredSize();
        component.setPreferredSize(new Dimension(size.width, height));
    }
}
