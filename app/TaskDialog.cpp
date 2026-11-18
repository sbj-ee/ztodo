#include "TaskDialog.hpp"

#include "DateText.hpp"
#include "Recurrence.hpp"
#include "Theme.hpp"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace ztodo {

namespace {
QIcon dot(const QColor &c)
{
    QPixmap pm(12, 12);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(c);
    p.drawEllipse(1, 1, 10, 10);
    return QIcon(pm);
}
} // namespace

QString TaskDialog::dueEditText(const QDate &date, const QTime &time, const QDate &today)
{
    if (!date.isValid()) {
        return {};
    }
    const QLocale c(QLocale::English);
    // Words for the near future; an ISO date otherwise, which never shifts
    // to next year the way "Nov 2" would once Nov 2 has passed.
    QString s = (date >= today.addDays(-1) && date < today.addDays(7)) ? formatDue(date, {}, today)
                                                                         : date.toString(Qt::ISODate);
    if (time.isValid()) {
        s += QLatin1Char(' ') + c.toString(time, QStringLiteral("h:mm AP"));
    }
    return s;
}

TaskDialog::TaskDialog(Store *store, const Task &task, bool isNew, QWidget *parent)
    : QDialog(parent)
    , m_store(store)
    , m_task(task)
{
    setObjectName(QStringLiteral("taskDialog"));
    setWindowTitle(isNew ? QStringLiteral("New Task") : QStringLiteral("Edit Task"));
    auto *layout = new QVBoxLayout(this);

    m_title = new QLineEdit(task.title);
    m_title->setObjectName(QStringLiteral("title"));
    m_title->setPlaceholderText(QStringLiteral("Task name"));
    QFont tf = m_title->font();
    tf.setPointSizeF(tf.pointSizeF() * 1.2);
    m_title->setFont(tf);
    layout->addWidget(m_title);

    m_notes = new QPlainTextEdit(task.notes);
    m_notes->setObjectName(QStringLiteral("notes"));
    m_notes->setPlaceholderText(QStringLiteral("Notes"));
    m_notes->setMinimumHeight(90);
    layout->addWidget(m_notes, 1);

    auto *form = new QFormLayout;
    const QDate today = QDate::currentDate();
    m_due = new QLineEdit(dueEditText(task.dueDate, task.dueTime, today));
    m_due->setObjectName(QStringLiteral("due"));
    m_due->setPlaceholderText(QStringLiteral("e.g. tomorrow 5pm, fri, nov 20, in 3 days"));
    m_dueHint = new QLabel;
    m_dueHint->setObjectName(QStringLiteral("dueHint"));
    auto *dueBox = new QVBoxLayout;
    dueBox->setSpacing(2);
    dueBox->addWidget(m_due);
    dueBox->addWidget(m_dueHint);
    // addRow(QString, QLayout*) creates a QLabel with no buddy, so '&' is
    // drawn literally. Set a buddy on an explicit label (like a QWidget field).
    auto *dueLabel = new QLabel(QStringLiteral("&Due:"));
    dueLabel->setObjectName(QStringLiteral("dueLabel"));
    dueLabel->setBuddy(m_due);
    form->addRow(dueLabel, dueBox);

    m_recur = new QLineEdit(task.recur);
    m_recur->setObjectName(QStringLiteral("recur"));
    m_recur->setPlaceholderText(QStringLiteral("e.g. every day, every mon and thu, every 15th, every! 2 weeks"));
    m_recurHint = new QLabel;
    auto *recurBox = new QVBoxLayout;
    recurBox->setSpacing(2);
    recurBox->addWidget(m_recur);
    recurBox->addWidget(m_recurHint);
    auto *recurLabel = new QLabel(QStringLiteral("&Repeat:"));
    recurLabel->setObjectName(QStringLiteral("recurLabel"));
    recurLabel->setBuddy(m_recur);
    form->addRow(recurLabel, recurBox);

    m_priority = new QComboBox;
    m_priority->setObjectName(QStringLiteral("priority"));
    const char *names[] = {"Priority 1", "Priority 2", "Priority 3", "Priority 4 (none)"};
    for (int p = 1; p <= 4; ++p) {
        m_priority->addItem(dot(theme::priority(p)), QString::fromLatin1(names[p - 1]), p);
    }
    m_priority->setCurrentIndex(std::clamp(task.priority, 1, 4) - 1);
    form->addRow(QStringLiteral("&Priority:"), m_priority);

    m_project = new QComboBox;
    m_project->setObjectName(QStringLiteral("project"));
    m_project->addItem(dot(theme::muted()), QStringLiteral("Inbox"), QString());
    for (const Project &p : store->projects()) {
        m_project->addItem(dot(QColor(p.color)), p.name, p.id);
    }
    m_project->setCurrentIndex(std::max(0, m_project->findData(task.project)));
    form->addRow(QStringLiteral("P&roject:"), m_project);

    QStringList labelNames;
    for (const QString &id : task.labels) {
        const QString n = store->labelName(id);
        if (!n.isEmpty()) {
            labelNames << n;
        }
    }
    m_labels = new QLineEdit(labelNames.join(QStringLiteral(", ")));
    m_labels->setObjectName(QStringLiteral("labels"));
    m_labels->setPlaceholderText(QStringLiteral("comma separated, e.g. errand, phone"));
    form->addRow(QStringLiteral("&Labels:"), m_labels);
    layout->addLayout(form);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    m_save = buttons->button(QDialogButtonBox::Save);
    if (!isNew) {
        QPushButton *del = buttons->addButton(QStringLiteral("&Delete"), QDialogButtonBox::DestructiveRole);
        del->setObjectName(QStringLiteral("delete"));
        connect(del, &QPushButton::clicked, this, [this] {
            if (QMessageBox::question(this, QStringLiteral("Delete Task"),
                                      QStringLiteral("Delete “%1” and its subtasks?").arg(m_task.title))
                == QMessageBox::Yes) {
                done(Deleted);
            }
        });
    }
    connect(buttons, &QDialogButtonBox::accepted, this, &TaskDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    connect(m_title, &QLineEdit::textChanged, this, &TaskDialog::validate);
    connect(m_due, &QLineEdit::textChanged, this, &TaskDialog::validate);
    connect(m_recur, &QLineEdit::textChanged, this, &TaskDialog::validate);
    validate();
    resize(520, 460);
}

void TaskDialog::validate()
{
    const QDate today = QDate::currentDate();
    bool ok = !m_title->text().trimmed().isEmpty();
    QDate d;
    QTime t;
    if (parseDue(m_due->text(), today, &d, &t)) {
        m_dueHint->setText(d.isValid() ? QStringLiteral("<small>%1</small>")
                                             .arg(QLocale(QLocale::English).toString(d, QStringLiteral("dddd, MMMM d yyyy"))
                                                  + (t.isValid() ? QStringLiteral(", ") + QLocale(QLocale::English).toString(t, QStringLiteral("h:mm AP")) : QString()))
                                       : QStringLiteral("<small>No date</small>"));
    } else {
        m_dueHint->setText(QStringLiteral("<small><font color=\"%1\">Not a date I understand</font></small>")
                               .arg(theme::overdue().name()));
        ok = false;
    }
    const QString rule = m_recur->text().trimmed();
    if (rule.isEmpty()) {
        m_recurHint->setText(QStringLiteral("<small>Doesn't repeat</small>"));
    } else if (Recurrence::parse(rule).valid) {
        m_recurHint->setText(QStringLiteral("<small>Repeats %1%2</small>")
                                 .arg(rule.toHtmlEscaped(),
                                      d.isValid() ? QString() : QStringLiteral(" (starting at the first occurrence)")));
    } else {
        m_recurHint->setText(QStringLiteral("<small><font color=\"%1\">Not a repeat rule I understand</font></small>")
                                 .arg(theme::overdue().name()));
        ok = false;
    }
    m_save->setEnabled(ok);
}

void TaskDialog::accept()
{
    const QDate today = QDate::currentDate();
    m_result = m_task;
    m_result.title = m_title->text().trimmed();
    m_result.notes = m_notes->toPlainText().trimmed();
    parseDue(m_due->text(), today, &m_result.dueDate, &m_result.dueTime);
    m_result.recur = m_recur->text().trimmed().toLower();
    if (!m_result.recur.isEmpty() && !m_result.dueDate.isValid()) {
        m_result.dueDate = Recurrence::parse(m_result.recur).first(today);
    }
    m_result.priority = m_priority->currentData().toInt();
    m_result.project = m_project->currentData().toString();
    m_result.labels.clear();
    for (const QString &raw : m_labels->text().split(QLatin1Char(','), Qt::SkipEmptyParts)) {
        QString name = raw.trimmed();
        if (name.startsWith(QLatin1Char('@'))) {
            name.remove(0, 1);
        }
        if (name.isEmpty()) {
            continue;
        }
        QString id = m_store->findLabel(name);
        if (id.isEmpty()) {
            id = m_store->addLabel(name);
        }
        if (!id.isEmpty() && !m_result.labels.contains(id)) {
            m_result.labels << id;
        }
    }
    QDialog::accept();
}

} // namespace ztodo
