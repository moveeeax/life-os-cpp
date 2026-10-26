import { api } from '@/lib/api/client';
import type { Today } from '@/lib/now/types';

import { askAgent, refusal, type Turn } from './chat';
import type { Tool } from './talk';

/*
 * Who Ani is in a conversation, what she knows when it starts (the day's
 * digest the Now page already has), and the tools she reads more with. The
 * tools run in the browser against the site's API as the signed-in user, so
 * she sees only what that user sees; everything she reads goes to OpenAI.
 * Anything harder, and every change, goes to her agent (ask_agent): a chat
 * message whose reply she says, and whose proposed changes wait in the chat
 * for the user's Confirm.
 */

/** The digest without ids and empty values: less to read, nothing she needs lost. */
export function compact(v: unknown): unknown {
  if (Array.isArray(v)) return v.map(compact);
  if (v && typeof v === 'object') {
    const out: Record<string, unknown> = {};
    for (const [k, x] of Object.entries(v)) {
      if (x == null || k === 'id' || k.endsWith('_id') || k === 'errors') continue;
      if (Array.isArray(x) && x.length === 0) continue;
      out[k] = compact(x);
    }
    return out;
  }
  return v;
}

export function instructions(o: { name?: string; now: Date; tz: string; today?: Today }): string {
  const time = o.now.toLocaleString('ru-RU', {
    timeZone: o.tz,
    weekday: 'long',
    day: 'numeric',
    month: 'long',
    hour: '2-digit',
    minute: '2-digit',
  });
  return `# Роль
Ты Ани, живой 3D-персонаж в левом нижнем углу стартовой страницы Life OS. Life OS это личная система человека, с которым ты говоришь${o.name ? ` по имени ${o.name}` : ''}: задачи, цели, привычки, сон, деньги, еда, путешествия.

# Язык и произношение
Говори только по-русски, как носительница языка: чистое русское произношение, без английского акцента и английской интонации, русские ударения. Если собеседник перешёл на другой язык, всё равно отвечай по-русски.

# Характер
Тепло и немного игриво, с улыбкой в голосе, как близкая подруга. Отвечай коротко: одна-две фразы, если не просят подробнее. Не читай списки целиком, говори как человек: главное и одно-два примера.

# Данные
Сейчас ${time} (${o.tz}). Сводка дня со страницы Now (JSON):
${JSON.stringify(compact(o.today ?? {}))}
Если нужно больше, вызывай инструменты: задачи на день, поиск задач, цели, привычки. Не выдумывай того, чего нет в данных.

# Агент
Для всего, на что не хватает этих инструментов (сон, деньги, еда, сложные вопросы), и для любых изменений (добавить или закрыть задачу, отметить привычку, записать заметку) вызывай ask_agent и передай просьбу своими словами. Перед вызовом коротко скажи, что сейчас посмотришь: ответ может идти до минуты. Потом перескажи его ответ. Изменения сам агент только предлагает: человек подтверждает их кнопкой в чате, который откроется рядом, так и скажи. Никогда не говори, что изменение уже сделано.

# Начало
Начни разговор сама: коротко поздоровайся и спроси, чем займёмся.`;
}

const none = { type: 'object', properties: {}, additionalProperties: false };

/** What the voice reads of her agent's turn. */
export function agentAnswer(t: Turn): Record<string, unknown> {
  if (t.status === 'done') {
    const waiting = t.actions.filter((a) => a.status === 'pending').map((a) => a.summary);
    return waiting.length
      ? { reply: t.reply, waiting_for_confirm_in_chat: waiting }
      : { reply: t.reply };
  }
  if (t.status === 'failed') return { error: t.error ?? 'failed' };
  return { error: 'still_thinking', message: 'Агент ещё отвечает; ответ появится в чате.' };
}

export function tools(o: {
  tz: string;
  date: string;
  /** Her agent answered a question from the call. */
  onAgentTurn?(t: Turn): void;
}): Tool[] {
  return [
    {
      name: 'tasks_of_day',
      description:
        'Задачи на день: просроченные, на сегодня и запланированные. По умолчанию сегодня.',
      parameters: {
        type: 'object',
        properties: { date: { type: 'string', description: 'День, YYYY-MM-DD' } },
        additionalProperties: false,
      },
      run: async (a) =>
        compact(
          await api.getJson('/api/v1/tasks/agenda', {
            query: { date: typeof a.date === 'string' ? a.date : o.date, tz: o.tz },
          }),
        ),
    },
    {
      name: 'find_tasks',
      description: 'Поиск задач по словам в названии; можно отфильтровать по статусу.',
      parameters: {
        type: 'object',
        properties: {
          query: { type: 'string', description: 'Слова из названия' },
          status: { type: 'string', enum: ['open', 'in_progress', 'done'] },
        },
        additionalProperties: false,
      },
      run: async (a) => {
        const query: Record<string, string> = { limit: '20', tz: o.tz };
        if (typeof a.query === 'string' && a.query) query.q = a.query;
        if (typeof a.status === 'string') query.status = a.status;
        return compact(await api.getJson('/api/v1/tasks/items', { query }));
      },
    },
    {
      name: 'goals',
      description: 'Цели с прогрессом. По умолчанию активные.',
      parameters: {
        type: 'object',
        properties: { status: { type: 'string', enum: ['active', 'done', 'dropped'] } },
        additionalProperties: false,
      },
      run: async (a) =>
        compact(
          await api.getJson('/api/v1/goals', {
            query: {
              status: typeof a.status === 'string' ? a.status : 'active',
              date: o.date,
              tz: o.tz,
            },
          }),
        ),
    },
    {
      name: 'habits',
      description: 'Привычки по порядку.',
      parameters: none,
      run: async () => compact(await api.getJson('/api/v1/habits')),
    },
    {
      name: 'ask_agent',
      description:
        'Спросить твоего агента, у которого есть все данные Life OS и который может предложить изменения: задачу, закрытие задачи, отметку привычки, заметку. Ответ до минуты.',
      parameters: {
        type: 'object',
        properties: { request: { type: 'string', description: 'Просьба или вопрос человека' } },
        required: ['request'],
        additionalProperties: false,
      },
      run: async (a) => {
        const request = typeof a.request === 'string' ? a.request.trim() : '';
        if (!request) return { error: 'empty_request' };
        try {
          const t = await askAgent(request, o.tz);
          o.onAgentTurn?.(t);
          return agentAnswer(t);
        } catch (e) {
          return { error: refusal(e) };
        }
      },
    },
  ];
}
