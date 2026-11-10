Food meal plan (migration 081): a template per weekday (`PUT /api/v1/food/plan/template`)
and an override for one day (`PUT` and `DELETE /api/v1/food/plan`); `GET /api/v1/food/plan?date=`
answers the day's override, else its weekday's template, sorted by time, each row `eaten` when the
diary has an entry of the same meal that day.
