#include "QueueToolTip.h"
#include "fatrat.h"

QueueToolTip::QueueToolTip(QWidget* parent, Queue* queue)
	: BaseToolTip(queue, parent), m_queue(queue)
{
}

void QueueToolTip::refresh()
{
	// Active: %1 down, %2 up
	// Waiting: %1 down, %2 up
	// Speed: %1 down, %2 up
	
	QString text;
	Queue::Stats& stats = m_queue->m_stats;
	
	//text = QString("<font color=green>Active:</font> %1 down | %2 up<br>"
	//		"<font color=red>Waiting:</font> %3 down | %4 up<br>"
	//		"<font color=blue>Speed:</font> %5 down | %6 up")
	//		.arg(ad).arg(au).arg(wd).arg(wu).arg(formatSize(sd, true)).arg(formatSize(su, true));
	text = QString("<table cellspacing=4><tr><td><font color=green>Active:</font></td><td>%1 down</td><td>%2 up</td></tr>"
			"<tr><td><font color=red>Waiting:</font></td><td>%3 down</td><td>%4 up</td></tr>"
			"<tr><td><font color=blue>Speed:</font></td><td>%5 down</td><td>%6 up</td></tr></table>")
			.arg(stats.active_d).arg(stats.active_u).arg(stats.waiting_d).arg(stats.waiting_u)
			.arg(formatSize(stats.down, true)).arg(formatSize(stats.up, true));
	setText(text);
	resize(sizeHint());
}

