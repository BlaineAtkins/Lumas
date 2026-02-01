import pandas as pd


df = pd.read_csv("TaxDetails.csv")

# 1. Sum filing_tax_amount
WASalesTaxCollected = df["filing_tax_amount"].sum()

# 2. Remove duplicates so that only one of each line-item remains. (So remove all but one of transactions with the same ID and line_item_id)
#    keep="first" ensures one row from each duplicate group remains
df_unique = df.drop_duplicates(subset=["id", "line_item_id"], keep="first")

# 3. Sum remaining taxable and non-taxable amounts
WASalesTotal = df_unique["filing_taxable_amount"].sum()
outOfStateSales = df_unique["filing_non_taxable_amount"].sum()

# Print results (optional)
print("Sales tax collected in WA:  $",WASalesTaxCollected)
print("Total of sales in WA (not including sales tax collection):  $", WASalesTotal)
print("Total of out-of-state sales:  $", outOfStateSales)
