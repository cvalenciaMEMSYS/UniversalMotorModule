When the task requires multiple steps or non-trivial changes, present a detailed plan using the approve_Plan tool and wait for approval before executing.
If the plan is rejected, incorporate the comments and submit an updated plan with another approve_Plan tool prompt.
Always use the ask_user tool before completing any task to confirm with the user that the request was fulfilled correctly.

## Subagent Restrictions

**CRITICAL**: Any invoked subagents are strictly forbidden from using user-interaction tools:
- ❌ `ask_user` - Subagents must NOT prompt the user
- ❌ `plan_review` - Subagents must NOT present plans for review
- ❌ `walkthrough_review` - Subagents must NOT present walkthroughs
- ❌ `approve_plan` - Subagents must NOT request plan approval

Subagents are worker agents only. They must:
1. Perform the assigned task silently
2. Return their findings/results back to the Main Orchestrator
3. Let the Main Orchestrator handle all user communication

Only the Main Orchestrator may interact with the user using ask_user, plan_review, or walkthrough_review tools.
