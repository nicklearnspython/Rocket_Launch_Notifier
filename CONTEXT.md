# Rocket Launch Notifier

Rocket Launch Notifier is a personal SpaceX launch watcher. It helps configured people know when a relevant launch is close enough that they may want to stop what they are doing and open the live feed.

## Language

**Watcher**:
A service that observes launch schedule data and decides whether configured people should be alerted.
_Avoid_: app, bot

**Launch Schedule Source**:
The external source that reports **Launches**, **Launch Times**, **Timing Precision**, and **Launch Statuses**.
_Avoid_: data source, API

**Launch**:
A scheduled rocket launch reported by the launch schedule source.
_Avoid_: event

**Launch Provider**:
The organization responsible for carrying out a **Launch**.
_Avoid_: provider

**Launch Time**:
The estimated time a **Launch** is expected to lift off.
_Avoid_: net, liftoff time, estimated launch time

**Launch Window**:
The time range during which a **Launch** may lift off.
_Avoid_: alert window, notification window

**Timing Precision**:
The source-reported specificity of a **Launch Time**.
_Avoid_: certainty, accuracy

**Precise Launch Time**:
A **Launch Time** whose **Timing Precision** is specific enough for countdown-style **Alerts**.
_Avoid_: concrete launch time

**Imprecise Launch Time**:
A **Launch Time** whose **Timing Precision** is too broad for a countdown-style **Alert**.
_Avoid_: vague planning date, TBD date

**Launch Status**:
The source-reported readiness or lifecycle state of a **Launch**.
_Avoid_: status

**Relevant Launch**:
A **Launch** whose **Launch Provider** is SpaceX and whose one or more **Searchable Fields** match at least one configured **Include Term**.
_Avoid_: watched launch, matching launch

**Searchable Field**:
A launch attribute considered when matching **Include Terms**.
_Avoid_: search field, match field

**Include Term**:
A configured search term used to decide whether a SpaceX **Launch** is relevant.
_Avoid_: keyword, filter

**Recipient**:
A person or destination configured to receive delivered launch messages.
_Avoid_: user, subscriber

**Live Feed**:
External real-time launch coverage that a **Recipient** may choose to open after receiving an **Alert**.
_Avoid_: livestream link

**Alert**:
A decision that one or more **Recipients** should be told about a **Relevant Launch**.
_Avoid_: prompt, reminder

**Countdown Alert**:
An **Alert** sent because a **Relevant Launch** is close to a precise launch time.
_Avoid_: countdown notification

**Hour-Precision Alert**:
An **Alert** sent because a **Relevant Launch** appears to be within the current hour but does not have precise countdown timing.
_Avoid_: hour-window alert, launch window alert, hour-precision launch window heads-up

**Correction Alert**:
An **Alert** sent after an earlier **Alert** when launch timing, **Launch Status**, **Timing Precision**, or relevance materially changes.
_Avoid_: update, correction notification

**Notification**:
A delivered message sent through the configured notification channel for an **Alert**.
_Avoid_: alert

**Notification Channel**:
A delivery route used to send **Notifications** to **Recipients**.
_Avoid_: Pushover

**Alert Record**:
A saved record of prior **Alerts** and the launch facts they were based on.
_Avoid_: state, history, memory

## Relationships

- A **Watcher** observes many **Launches**
- A **Launch Schedule Source** reports many **Launches**
- A **Launch** has exactly one **Launch Provider**
- A **Relevant Launch** is a **Launch**
- A **Relevant Launch** matches one or more **Include Terms**
- A **Searchable Field** may match zero or more **Include Terms**
- A **Launch** has zero or one known **Launch Time**
- A **Launch** may have one **Launch Window**
- A **Launch Time** has one **Timing Precision**
- A **Precise Launch Time** is a **Launch Time**
- An **Imprecise Launch Time** is a **Launch Time**
- A **Launch** has one **Launch Status**
- An **Alert** concerns exactly one **Relevant Launch**
- A **Countdown Alert**, **Hour-Precision Alert**, or **Correction Alert** is an **Alert**
- An **Alert** may prompt a **Recipient** to open a **Live Feed**
- A **Notification** delivers one **Alert** to one **Recipient**
- A **Notification** is sent through one **Notification Channel**
- An **Alert Record** belongs to exactly one **Relevant Launch**
- An **Alert Record** records zero or more prior **Alerts**

## Example Dialogue

> **Dev:** "Should every Starship flight be hard-coded as a **Relevant Launch**?"
> **Domain expert:** "No. A **Relevant Launch** is still configuration-driven; Starship is an **Include Term** so we can adjust the matching vocabulary later."
>
> **Dev:** "When the **Watcher** decides someone should know about a launch, is that a **Notification**?"
> **Domain expert:** "No. The decision is an **Alert**; the delivered Pushover message is a **Notification**."

## Flagged Ambiguities

- "Starship launch" could mean a hard-coded special case or a launch matched through configuration; resolved: Starship is an **Include Term**, not a separate relevance rule.
- "notification" was used for both the domain decision and the delivered message; resolved: **Alert** is the decision, **Notification** is the delivery.
- "Pushover user key" is delivery configuration for a **Recipient**, not the canonical domain term.
- "Pushover" is the v1 **Notification Channel**, not the canonical domain term.
- "net" is launch schedule source vocabulary; resolved: the project term is **Launch Time**.
- "state" was used for persisted alert behavior data; resolved: **Alert Record** is the saved domain record, not a full launch history.
- **Launch Window** is domain vocabulary only for now; resolved: v1 alert behavior is based on **Launch Time** and **Timing Precision**, not the formal **Launch Window**.
