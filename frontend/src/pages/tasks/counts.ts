import { useAgenda, useNotes } from '@/hooks/useTasks';
import { localToday, localZone } from '@/lib/tasks';

/** The agenda of today in this browser's zone and the tab counts every page shows. */
export function useTasksDay() {
  const today = localToday();
  const tz = localZone();
  const agenda = useAgenda(today, tz);
  const notes = useNotes('inbox');
  const a = agenda.data;
  const counts = {
    today: a ? a.late.length + a.soon.length : 0,
    later: a ? a.dated.length + a.someday.length : 0,
    inbox: notes.data?.length ?? 0,
  };
  return { today, agenda, notes, counts };
}
